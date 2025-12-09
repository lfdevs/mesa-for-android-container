/*
 * Copyright © 2018 Google, Inc.
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "tu_knl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "vk_util.h"

#include "drm-uapi/msm_drm.h"
#include "util/u_debug.h"
#include "util/u_process.h"
#include "util/hash_table.h"
#include "util/libsync.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_dynamic_rendering.h"
#include "tu_knl_drm.h"
#include "tu_queue.h"
#include "tu_rmv.h"
#include "redump.h"

static int
tu_drm_get_param(int fd, uint32_t param, uint64_t *value)
{
   /* Technically this requires a pipe, but the kernel only supports one pipe
    * anyway at the time of writing and most of these are clearly pipe
    * independent. */
   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
   };

   int ret = drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &req, sizeof(req));
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

static int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_GPU_ID, &value);
   if (ret)
      return ret;

   *id = value;
   return 0;
}

static int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_GMEM_SIZE, &value);
   if (ret)
      return ret;

   *size = value;
   return 0;
}

static int
tu_drm_get_gmem_base(const struct tu_physical_device *dev, uint64_t *base)
{
   return tu_drm_get_param(dev->local_fd, MSM_PARAM_GMEM_BASE, base);
}

static bool
tu_drm_get_raytracing(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_RAYTRACING, &value);
   if (ret)
      return false;

   return value;
}

static bool
tu_drm_get_prr(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_HAS_PRR, &value);
   if (ret)
      return false;

   return value;
}

static int
tu_drm_get_va_prop(const struct tu_physical_device *dev,
                   uint64_t *va_start, uint64_t *va_size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_VA_START, &value);
   if (ret)
      return ret;

   *va_start = value;

   ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_VA_SIZE, &value);
   if (ret)
      return ret;

   *va_size = value;

   return 0;
}

static bool
tu_drm_has_preemption(const struct tu_physical_device *dev)
{
   struct drm_msm_submitqueue req = {
      .flags = MSM_SUBMITQUEUE_ALLOW_PREEMPT,
      .prio = dev->submitqueue_priority_count / 2,
   };

   int ret = drmCommandWriteRead(dev->local_fd,
                                 DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
   if (ret)
      return false;

   drmCommandWrite(dev->local_fd, DRM_MSM_SUBMITQUEUE_CLOSE, &req.id,
                   sizeof(req.id));
   return true;
}

static int
tu_drm_set_param(int fd, uint32_t param, uint64_t value, uint32_t len)
{
   struct drm_msm_param param_req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
      .value = value,
      .len = len,
   };

   int ret = drmCommandWriteRead(fd, DRM_MSM_SET_PARAM, &param_req,
                                 sizeof(param_req));
   return ret;
}

static int
tu_try_enable_vm_bind(int fd)
{
   return tu_drm_set_param(fd, MSM_PARAM_EN_VM_BIND, 1, 0);
}

static void
tu_drm_set_debuginfo(int fd)
{
   if (!TU_DEBUG(COMM))
      return;

   const char *comm = util_get_process_name();
   if (comm)
      tu_drm_set_param(fd, MSM_PARAM_COMM, (uintptr_t)comm, strlen(comm));

   static char cmdline[0x1000];
   if (util_get_command_line(cmdline, sizeof(cmdline)))
      tu_drm_set_param(fd, MSM_PARAM_CMDLINE, (uintptr_t)cmdline, strlen(cmdline));
}

static uint32_t
tu_drm_get_priorities(const struct tu_physical_device *dev)
{
   uint64_t val = 1;
   tu_drm_get_param(dev->local_fd, MSM_PARAM_PRIORITIES, &val);
   assert(val >= 1);

   return val;
}

static uint32_t
tu_drm_get_highest_bank_bit(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_HIGHEST_BANK_BIT, &value);
   if (ret)
      return 0;

   return value;
}

static enum fdl_macrotile_mode
tu_drm_get_macrotile_mode(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_MACROTILE_MODE, &value);
   if (ret)
      return FDL_MACROTILE_INVALID;

   return (enum fdl_macrotile_mode) value;
}

static uint32_t
tu_drm_get_ubwc_swizzle(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_UBWC_SWIZZLE, &value);
   if (ret)
      return ~0;

   return value;
}

static uint64_t
tu_drm_get_uche_trap_base(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_UCHE_TRAP_BASE, &value);
   if (ret)
      return 0x1fffffffff000ull;

   return value;
}

static bool
tu_drm_is_memory_type_supported(int fd, uint32_t flags)
{
   struct drm_msm_gem_new req_alloc = { .size = 0x1000, .flags = flags };

   int ret =
      drmCommandWriteRead(fd, DRM_MSM_GEM_NEW, &req_alloc, sizeof(req_alloc));
   if (ret) {
      return false;
   }

   struct drm_gem_close req_close = {
      .handle = req_alloc.handle,
   };
   drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &req_close);

   return true;
}

static VkResult
msm_device_init(struct tu_device *dev)
{
   int fd = open(dev->physical_device->fd_path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return vk_startup_errorf(
            dev->physical_device->instance, VK_ERROR_INITIALIZATION_FAILED,
            "failed to open device %s", dev->physical_device->fd_path);
   }

   int ret;
   if (dev->physical_device->has_vm_bind) {
      ret = tu_try_enable_vm_bind(fd);
      if (ret != 0) {
         return vk_startup_errorf(dev->physical_device->instance,
                                  VK_ERROR_INITIALIZATION_FAILED,
                                  "Failed to enable VM_BIND mode: %d", ret);
      }

      struct drm_msm_submitqueue submit_req = {
         .flags = MSM_SUBMITQUEUE_VM_BIND,
      };

      ret = drmCommandWriteRead(fd, DRM_MSM_SUBMITQUEUE_NEW, &submit_req,
                                sizeof(submit_req));
      if (ret != 0) {
         close(fd);
         return vk_startup_errorf(dev->physical_device->instance,
                                  VK_ERROR_INITIALIZATION_FAILED,
                                  "Failed to create VM_BIND queue: %d", ret);
      }

      dev->vm_bind_queue_id = submit_req.id;
   }

   tu_drm_set_debuginfo(fd);

   ret = tu_drm_get_param(fd, MSM_PARAM_FAULTS, &dev->fault_count);
   if (ret != 0) {
      close(fd);
      return vk_startup_errorf(dev->physical_device->instance,
                               VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to get initial fault count: %d", ret);
   }

   dev->vk.copy_sync_payloads = vk_drm_syncobj_copy_payloads;
   dev->fd = fd;

   return VK_SUCCESS;
}

static void
msm_device_finish(struct tu_device *dev)
{
   close(dev->fd);
}

static int
msm_device_get_gpu_timestamp(struct tu_device *dev, uint64_t *ts)
{
   return tu_drm_get_param(dev->fd, MSM_PARAM_TIMESTAMP, ts);
}

static int
msm_device_get_suspend_count(struct tu_device *dev, uint64_t *suspend_count)
{
   int ret = tu_drm_get_param(dev->fd, MSM_PARAM_SUSPENDS, suspend_count);
   return ret;
}

static VkResult
msm_device_check_status(struct tu_device *device)
{
   uint64_t last_fault_count = device->fault_count;
   int ret = tu_drm_get_param(device->fd, MSM_PARAM_FAULTS, &device->fault_count);
   if (ret != 0)
      return vk_device_set_lost(&device->vk, "error getting GPU fault count: %d", ret);

   if (last_fault_count != device->fault_count)
      return vk_device_set_lost(&device->vk, "GPU faulted or hung");

   return VK_SUCCESS;
}

static int
msm_submitqueue_new(struct tu_device *dev, struct tu_queue *queue)
{
   int ret = 0;

   if (queue->type == TU_QUEUE_GFX) {
      assert(queue->priority >= 0 &&
             queue->priority < dev->physical_device->submitqueue_priority_count);
      struct drm_msm_submitqueue req = {
         .flags = (dev->physical_device->info->chip >= 7 &&
                   dev->physical_device->has_preemption ?
                   MSM_SUBMITQUEUE_ALLOW_PREEMPT : 0),
         .prio = queue->priority,
      };

      ret = drmCommandWriteRead(dev->fd,
                                DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
      if (ret)
         return ret;

      queue->msm_queue_id = req.id;
   }

   if (queue->type == TU_QUEUE_SPARSE ||
       dev->physical_device->has_vm_bind) {
      struct drm_msm_submitqueue req = {
         .flags = MSM_SUBMITQUEUE_VM_BIND,
         .prio = 0,
      };

      ret = drmCommandWriteRead(dev->fd,
                                DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
      if (ret)
         goto fail_sparse;

      queue->sparse_queue_id = req.id;
   }

   if (queue->type == TU_QUEUE_GFX && dev->physical_device->has_vm_bind) {
      ret = drmSyncobjCreate(dev->fd, 0, &queue->gfx_syncobj);
      if (ret)
         goto fail_gfx_syncobj;
      ret = drmSyncobjCreate(dev->fd, 0, &queue->sparse_syncobj);
      if (ret)
         goto fail_sparse_syncobj;
   }

   return 0;

fail_sparse_syncobj:
   drmSyncobjDestroy(dev->fd, queue->gfx_syncobj);
fail_gfx_syncobj:
   if (queue->type == TU_QUEUE_SPARSE || dev->physical_device->has_vm_bind) {
      drmCommandWrite(dev->fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                      &queue->sparse_queue_id, sizeof(uint32_t));
   }
fail_sparse:
   if (queue->type == TU_QUEUE_GFX) {
      drmCommandWrite(dev->fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                      &queue->msm_queue_id, sizeof(uint32_t));
   }

   return ret;
}

static void
msm_submitqueue_close(struct tu_device *dev, struct tu_queue *queue)
{
   if (queue->msm_queue_id) {
      drmCommandWrite(dev->fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                      &queue->msm_queue_id, sizeof(uint32_t));
   }

   if (queue->sparse_queue_id) {
      drmCommandWrite(dev->fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                      &queue->sparse_queue_id, sizeof(uint32_t));
   }

   if (queue->type == TU_QUEUE_GFX && dev->physical_device->has_vm_bind) {
      drmSyncobjDestroy(dev->fd, queue->sparse_syncobj);
      drmSyncobjDestroy(dev->fd, queue->gfx_syncobj);
   }
}

static void
tu_gem_close(const struct tu_device *dev, uint32_t gem_handle)
{
   struct drm_gem_close req = {
      .handle = gem_handle,
   };

   drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
}

/** Helper for DRM_MSM_GEM_INFO, returns 0 on error. */
static uint64_t
tu_gem_info(const struct tu_device *dev, uint32_t gem_handle, uint32_t info)
{
   struct drm_msm_gem_info req = {
      .handle = gem_handle,
      .info = info,
   };

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret < 0)
      return 0;

   return req.value;
}

static VkResult
tu_wait_fence(struct tu_device *dev,
              uint32_t queue_id,
              int fence,
              uint64_t timeout_ns)
{
   MESA_TRACE_FUNC();
   /* fence was created when no work was yet submitted */
   if (fence < 0)
      return VK_SUCCESS;

   struct drm_msm_wait_fence req = {
      .fence = fence,
      .queueid = queue_id,
   };
   int ret;

   get_abs_timeout(&req.timeout, timeout_ns);

   ret = drmCommandWrite(dev->fd, DRM_MSM_WAIT_FENCE, &req, sizeof(req));
   if (ret) {
      if (ret == -ETIMEDOUT) {
         return VK_TIMEOUT;
      } else {
         mesa_loge("tu_wait_fence failed! %d (%s)", ret, strerror(errno));
         return VK_ERROR_UNKNOWN;
      }
   }

   return VK_SUCCESS;
}

VkResult
msm_queue_wait_fence(struct tu_queue *queue, uint32_t fence,
                     uint64_t timeout_ns)
{
   return tu_wait_fence(queue->device, queue->msm_queue_id, fence,
                        timeout_ns);
}

static VkResult
tu_free_zombie_vma_locked(struct tu_device *dev, bool wait)
{
   if (!u_vector_length(&dev->zombie_vmas))
      return VK_SUCCESS;

   MESA_TRACE_FUNC();

   if (wait) {
      struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
            u_vector_head(&dev->zombie_vmas);
      /* Wait for 3s (arbitrary timeout) */
      VkResult ret = tu_wait_fence(dev, dev->queues[0]->msm_queue_id,
                                   vma->fence, 3000000000);

      if (ret != VK_SUCCESS)
         return ret;
   }

   int last_signaled_fence = -1;
   while (u_vector_length(&dev->zombie_vmas) > 0) {
      struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
            u_vector_tail(&dev->zombie_vmas);
      if (vma->fence > last_signaled_fence) {
         VkResult ret =
            tu_wait_fence(dev, dev->queues[0]->msm_queue_id, vma->fence, 0);
         if (ret != VK_SUCCESS)
            return ret;

         last_signaled_fence = vma->fence;
      }

      if (vma->gem_handle) {
         /* Ensure that internal kernel's vma is freed. */
         struct drm_msm_gem_info req = {
            .handle = vma->gem_handle,
            .info = MSM_INFO_SET_IOVA,
            .value = 0,
         };

         int ret =
            drmCommandWriteRead(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
         if (ret < 0) {
            mesa_loge("MSM_INFO_SET_IOVA(0) failed! %d (%s)", ret,
                      strerror(errno));
            return VK_ERROR_UNKNOWN;
         }

         tu_gem_close(dev, vma->gem_handle);

         util_vma_heap_free(&dev->vma, vma->iova, vma->size);
      }

      u_vector_remove(&dev->zombie_vmas);
   }

   return VK_SUCCESS;
}

static bool
tu_restore_from_zombie_vma_locked(struct tu_device *dev,
                                  uint32_t gem_handle,
                                  uint64_t *iova)
{
   struct tu_zombie_vma *vma;
   u_vector_foreach (vma, &dev->zombie_vmas) {
      if (vma->gem_handle == gem_handle) {
         *iova = vma->iova;

         /* mark to skip later gem and iova cleanup */
         vma->gem_handle = 0;
         return true;
      }
   }

   return false;
}

static VkResult
msm_allocate_userspace_iova_locked(struct tu_device *dev,
                                   uint32_t gem_handle,
                                   uint64_t size,
                                   uint64_t client_iova,
                                   enum tu_bo_alloc_flags flags,
                                   uint64_t *iova)
{
   VkResult result;

   *iova = 0;

   if ((flags & TU_BO_ALLOC_DMABUF) &&
       tu_restore_from_zombie_vma_locked(dev, gem_handle, iova))
      return VK_SUCCESS;

   tu_free_zombie_vma_locked(dev, false);

   result = tu_allocate_userspace_iova(dev, size, client_iova, flags, iova);
   if (result == VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS) {
      /* Address may be already freed by us, but not considered as
       * freed by the kernel. We have to wait until all work that
       * may hold the address is done. Since addresses are meant to
       * be replayed only by debug tooling, it should be ok to wait.
       */
      tu_free_zombie_vma_locked(dev, true);
      result = tu_allocate_userspace_iova(dev, size, client_iova, flags, iova);
   }

   return result;
}

static VkResult
msm_set_iova(struct tu_device *dev,
             uint32_t gem_handle,
             uint64_t iova)
{
   struct drm_msm_gem_info req = {
      .handle = gem_handle,
      .info = MSM_INFO_SET_IOVA,
      .value = iova,
   };

   int ret =
      drmCommandWriteRead(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret < 0) {
      mesa_loge("MSM_INFO_SET_IOVA failed! %d (%s)", ret, strerror(errno));
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

static VkResult
msm_get_iova(struct tu_device *dev,
             uint32_t gem_handle,
             uint64_t *iova)
{
   *iova = tu_gem_info(dev, gem_handle, MSM_INFO_GET_IOVA);
   if (!*iova)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   return VK_SUCCESS;
}

/* Performs a VM_BIND mapping operation on the driver-internal VM_BIND queue
 * from the BO memory to an iova range.  No in fences are provided, so the CPU
 * may proceed with the operation immediately (and thus, unmap operations need
 * to be held off until GPU access to them are done, or faults may occur).  An
 * out fence is requested, so that all future queue submits will wait for the
 * map to complete.
 *
 * Since all map/unmap operations happen in order, we don't need to track zombie
 * VMAs between when they're unmapped from our perspective (but not unmapped
 * by the kernel) and when they can be remapped, unlike the old set_iova path.
 */
static VkResult
tu_map_vm_bind(struct tu_device *dev, uint32_t map_op, uint32_t map_op_flags,
               uint64_t iova, uint32_t gem_handle, uint64_t bo_offset,
               uint64_t range)
{
   struct drm_msm_vm_bind req = {
      .flags = MSM_VM_BIND_FENCE_FD_OUT,
      .nr_ops = 1,
      .queue_id = dev->vm_bind_queue_id,
      .op_stride = sizeof(drm_msm_vm_bind_op),
      .op = {
         .op = map_op,
         .handle = gem_handle,
         .obj_offset = bo_offset,
         .iova = iova,
         .range = range,
         .flags = map_op_flags,
      },
   };

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_VM_BIND,
                                 &req, sizeof(req));

   /* When failing to map a BO, the kernel marks the VM as dead */
   if (ret)
      return vk_device_set_lost(&dev->vk, "BO map failed: %m");

   int old_fence;
   u_rwlock_wrlock(&dev->vm_bind_fence_lock);
   old_fence = dev->vm_bind_fence_fd;
   dev->vm_bind_fence_fd = req.fence_fd;
   u_rwlock_wrunlock(&dev->vm_bind_fence_lock);

   if (old_fence != -1)
      close(old_fence);

   return VK_SUCCESS;
}

static VkResult
msm_allocate_vm_bind(struct tu_device *dev,
                     uint32_t gem_handle,
                     uint64_t size,
                     uint64_t iova,
                     enum tu_bo_alloc_flags flags)
{
   uint32_t map_op_flags = 0;
   if (flags & TU_BO_ALLOC_ALLOW_DUMP)
      map_op_flags |= MSM_VM_BIND_OP_DUMP;
   return tu_map_vm_bind(dev, MSM_VM_BIND_OP_MAP, map_op_flags, iova,
                         gem_handle, 0, size);
}

static VkResult
tu_bo_add_to_bo_list(struct tu_device *dev,
                     uint32_t gem_handle, uint32_t flags, uint64_t iova,
                     uint32_t *bo_list_idx)
{
   uint32_t idx = dev->submit_bo_count++;

   /* grow the bo list if needed */
   if (idx >= dev->submit_bo_list_size) {
      uint32_t new_len = idx + 64;
      struct drm_msm_gem_submit_bo *new_ptr = (struct drm_msm_gem_submit_bo *)
         vk_realloc(&dev->vk.alloc, dev->submit_bo_list, new_len * sizeof(*dev->submit_bo_list),
                    8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!new_ptr) {
         dev->submit_bo_count--;
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      dev->submit_bo_list = new_ptr;
      dev->submit_bo_list_size = new_len;
   }

   bool dump = flags & TU_BO_ALLOC_ALLOW_DUMP;
   bool implicit_sync = flags & TU_BO_ALLOC_IMPLICIT_SYNC;
   dev->submit_bo_list[idx] = (struct drm_msm_gem_submit_bo) {
      .flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE |
               COND(dump, MSM_SUBMIT_BO_DUMP) |
               COND(!implicit_sync, MSM_SUBMIT_BO_NO_IMPLICIT),
      .handle = gem_handle,
      .presumed = iova,
   };

   if (implicit_sync)
      dev->implicit_sync_bo_count++;

   *bo_list_idx = idx;
   return VK_SUCCESS;
}

static inline void
msm_vma_lock(struct tu_device *dev)
{
   if (dev->physical_device->has_set_iova)
      mtx_lock(&dev->vma_mutex);
}

static inline void
msm_vma_unlock(struct tu_device *dev)
{
   if (dev->physical_device->has_set_iova)
      mtx_unlock(&dev->vma_mutex);
}

/* If we have control over iova, allocate the iova range. */
static VkResult
tu_allocate_iova(struct tu_device *dev,
                 uint32_t gem_handle, /* only for BOs imported via dma-buf */
                 uint64_t size,
                 uint64_t client_iova,
                 enum tu_bo_alloc_flags flags,
                 uint64_t *iova)
{
   VkResult result = VK_SUCCESS;

   msm_vma_lock(dev);

   if (dev->physical_device->has_vm_bind) {
      result = tu_allocate_userspace_iova(dev, size, client_iova, flags, iova);
   } else if (dev->physical_device->has_set_iova) {
      assert(dev->physical_device->has_set_iova);
      result = msm_allocate_userspace_iova_locked(dev, gem_handle, size,
                                                  client_iova, flags, iova);
   }

   msm_vma_unlock(dev);

   return result;
}

void
tu_free_iova(struct tu_device *dev,
             uint64_t iova,
             uint64_t size)
{
   msm_vma_lock(dev);

   if (dev->physical_device->has_set_iova)
      util_vma_heap_free(&dev->vma, iova, size);

   msm_vma_unlock(dev);
}

static VkResult
tu_bo_init(struct tu_device *dev,
           struct vk_object_base *base,
           struct tu_bo *bo,
           uint32_t gem_handle,
           uint64_t size,
           uint64_t iova,
           enum tu_bo_alloc_flags flags,
           const char *name)
{
   VkResult result = VK_SUCCESS;

   if (dev->physical_device->has_vm_bind) {
      result = msm_allocate_vm_bind(dev, gem_handle, size, iova, flags);
   } else if (dev->physical_device->has_set_iova) {
      result = msm_set_iova(dev, gem_handle, iova);
   } else {
      result = msm_get_iova(dev, gem_handle, &iova);
   }

   if (result != VK_SUCCESS) {
      tu_gem_close(dev, gem_handle);
      return result;
   }

   name = tu_debug_bos_add(dev, size, name);

   uint32_t idx = 0;

   if (!dev->physical_device->has_vm_bind) {
      mtx_lock(&dev->bo_mutex);

      result = tu_bo_add_to_bo_list(dev, gem_handle, flags, iova, &idx);
      if (result != VK_SUCCESS) {
         mtx_unlock(&dev->bo_mutex);
         tu_gem_close(dev, gem_handle);
         return result;
      }
   }

   bool implicit_sync = flags & TU_BO_ALLOC_IMPLICIT_SYNC;
   *bo = (struct tu_bo) {
      .gem_handle = gem_handle,
      .size = size,
      .iova = iova,
      .name = name,
      .refcnt = 1,
      .submit_bo_list_idx = idx,
      .implicit_sync = implicit_sync,
      .base = base,
   };

   if (!dev->physical_device->has_vm_bind)
      mtx_unlock(&dev->bo_mutex);

   tu_dump_bo_init(dev, bo);

   TU_RMV(bo_allocate, dev, bo);

   return VK_SUCCESS;
}

/**
 * Sets the name in the kernel so that the contents of /debug/dri/0/gem are more
 * useful.
 *
 * We skip this on release builds (when we're also not doing BO debugging) to
 * reduce overhead.
 */
static void
tu_bo_set_kernel_name(struct tu_device *dev, struct tu_bo *bo, const char *name)
{
   bool kernel_bo_names = dev->bo_sizes != NULL;
#if MESA_DEBUG
   kernel_bo_names = true;
#endif
   if (!kernel_bo_names)
      return;

   struct drm_msm_gem_info req = {
      .handle = bo->gem_handle,
      .info = MSM_INFO_SET_NAME,
      .value = (uintptr_t)(void *)name,
      .len = strlen(name),
   };

   int ret = drmCommandWrite(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret) {
      mesa_logw_once("Failed to set BO name with DRM_MSM_GEM_INFO: %d",
                     ret);
   }
}

static VkResult
msm_bo_init(struct tu_device *dev,
            struct vk_object_base *base,
            struct tu_bo **out_bo,
            uint64_t size,
            uint64_t client_iova,
            VkMemoryPropertyFlags mem_property,
            enum tu_bo_alloc_flags flags,
            struct tu_sparse_vma *lazy_vma,
            const char *name)
{
   MESA_TRACE_FUNC();
   VkResult result = VK_SUCCESS;
   uint64_t iova;

   if (lazy_vma)
      iova = lazy_vma->msm.iova;
   else
      result = tu_allocate_iova(dev, 0, size, client_iova, flags, &iova);

   if (result != VK_SUCCESS)
      return result;

   struct drm_msm_gem_new req = {
      .size = size,
      .flags = 0
   };

   if (mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      if (mem_property & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
         req.flags |= MSM_BO_CACHED_COHERENT;
      } else {
         req.flags |= MSM_BO_CACHED;
      }
   } else {
      req.flags |= MSM_BO_WC;
   }

   if (flags & TU_BO_ALLOC_GPU_READ_ONLY)
      req.flags |= MSM_BO_GPU_READONLY;

   if (dev->physical_device->has_vm_bind && !(flags & TU_BO_ALLOC_SHAREABLE))
      req.flags |= MSM_BO_NO_SHARE;

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_GEM_NEW, &req, sizeof(req));
   if (ret) {
      if (!lazy_vma)
         tu_free_iova(dev, iova, size);
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   struct tu_bo* bo = tu_device_lookup_bo(dev, req.handle);
   assert(bo && bo->gem_handle == 0);

   assert(!(flags & TU_BO_ALLOC_DMABUF));

   result =
      tu_bo_init(dev, base, bo, req.handle, size, iova, flags, name);

   if (result == VK_SUCCESS) {
      *out_bo = bo;
      if (flags & TU_BO_ALLOC_INTERNAL_RESOURCE) {
         TU_RMV(internal_resource_create, dev, bo);
         TU_RMV(resource_name, dev, bo, name);
      }
      if (lazy_vma) {
         lazy_vma->msm.backs_lazy_bo = true;
         bo->lazy = true;
      }
   } else {
      if (!lazy_vma)
         tu_free_iova(dev, iova, size);
      memset(bo, 0, sizeof(*bo));
   }

   /* We don't use bo->name here because for the !TU_DEBUG=bo case bo->name is NULL. */
   tu_bo_set_kernel_name(dev, bo, name);

   if (result == VK_SUCCESS &&
       (mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
       !(mem_property & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      tu_bo_map(dev, bo, NULL);

      /* Cached non-coherent memory may already have dirty cache lines,
       * we should clean the cache lines before GPU got the chance to
       * write into this memory.
       *
       * MSM already does this automatically for uncached (MSM_BO_WC) memory.
       */
      tu_bo_sync_cache(dev, bo, 0, VK_WHOLE_SIZE, TU_MEM_SYNC_CACHE_TO_GPU);
   }

   return result;
}

static VkResult
msm_bo_init_dmabuf(struct tu_device *dev,
                   struct tu_bo **out_bo,
                   uint64_t size,
                   int prime_fd)
{
   /* lseek() to get the real size */
   off_t real_size = lseek(prime_fd, 0, SEEK_END);
   lseek(prime_fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size)
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   /* iova allocation needs to consider the object's *real* size: */
   size = real_size;

   /* Importing the same dmabuf several times would yield the same
    * gem_handle. Thus there could be a race when destroying
    * BO and importing the same dmabuf from different threads.
    * We must not permit the creation of dmabuf BO and its release
    * to happen in parallel.
    */
   u_rwlock_wrlock(&dev->dma_bo_lock);

   uint32_t gem_handle;
   int ret = drmPrimeFDToHandle(dev->fd, prime_fd,
                                &gem_handle);
   if (ret) {
      u_rwlock_wrunlock(&dev->dma_bo_lock);
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }

   uint64_t iova;

   struct tu_bo* bo = tu_device_lookup_bo(dev, gem_handle);

   if (bo->refcnt != 0) {
      p_atomic_inc(&bo->refcnt);
      u_rwlock_wrunlock(&dev->dma_bo_lock);

      *out_bo = bo;
      return VK_SUCCESS;
   }

   VkResult result =
      tu_allocate_iova(dev, gem_handle, size, 0, TU_BO_ALLOC_DMABUF, &iova);

   if (result != VK_SUCCESS) {
      tu_gem_close(dev, gem_handle);
      goto out_unlock;
   }

   result =
      tu_bo_init(dev, NULL, bo, gem_handle, size, iova, TU_BO_ALLOC_DMABUF, "dmabuf");

   if (result != VK_SUCCESS) {
      tu_free_iova(dev, iova, size);
      memset(bo, 0, sizeof(*bo));
   } else {
      *out_bo = bo;
   }

out_unlock:
   u_rwlock_wrunlock(&dev->dma_bo_lock);
   return result;
}

static VkResult
msm_bo_map(struct tu_device *dev, struct tu_bo *bo, void *placed_addr)
{
   uint64_t offset = tu_gem_info(dev, bo->gem_handle, MSM_INFO_GET_OFFSET);
   if (!offset)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   /* TODO: Should we use the wrapper os_mmap() like Freedreno does? */
   void *map = mmap(placed_addr, bo->size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | (placed_addr != NULL ? MAP_FIXED : 0),
                    dev->fd, offset);
   if (map == MAP_FAILED)
      return vk_error(dev, VK_ERROR_MEMORY_MAP_FAILED);

   bo->map = map;
   TU_RMV(bo_map, dev, bo);

   return VK_SUCCESS;
}

static void
msm_bo_allow_dump(struct tu_device *dev, struct tu_bo *bo)
{
   if (dev->physical_device->has_vm_bind) {
      tu_map_vm_bind(dev, MSM_VM_BIND_OP_MAP, MSM_VM_BIND_OP_DUMP,
                     bo->iova, bo->gem_handle, 0, bo->size);
   } else {
      mtx_lock(&dev->bo_mutex);
      dev->submit_bo_list[bo->submit_bo_list_idx].flags |= MSM_SUBMIT_BO_DUMP;
      mtx_unlock(&dev->bo_mutex);
   }
}


static void
msm_bo_set_metadata(struct tu_device *dev, struct tu_bo *bo,
                    void *metadata, uint32_t metadata_size)
{
   struct drm_msm_gem_info req = {
      .handle = bo->gem_handle,
      .info = MSM_INFO_SET_METADATA,
      .value = (uintptr_t)(void *)metadata,
      .len = metadata_size,
   };

   int ret = drmCommandWrite(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret) {
      mesa_logw_once("Failed to set BO metadata with DRM_MSM_GEM_INFO: %d",
                     ret);
   }
}

static int
msm_bo_get_metadata(struct tu_device *dev, struct tu_bo *bo,
                    void *metadata, uint32_t metadata_size)
{
   struct drm_msm_gem_info req = {
      .handle = bo->gem_handle,
      .info = MSM_INFO_GET_METADATA,
      .value = (uintptr_t)(void *)metadata,
      .len = metadata_size,
   };

   int ret = drmCommandWrite(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret) {
      mesa_logw_once("Failed to get BO metadata with DRM_MSM_GEM_INFO: %d",
                     ret);
   }

   return ret;
}

static void
msm_bo_gem_close(struct tu_device *dev, struct tu_bo *bo)
{
   /* Our BO structs are stored in a sparse array in the physical device,
    * so we don't want to free the BO pointer, instead we want to reset it
    * to 0, to signal that array entry as being free.
    */
   uint32_t gem_handle = bo->gem_handle;
   memset(bo, 0, sizeof(*bo));

   struct drm_gem_close req = {
      .handle = gem_handle,
   };

   drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
}

static void
msm_bo_finish(struct tu_device *dev, struct tu_bo *bo)
{
   assert(bo->gem_handle);

   u_rwlock_rdlock(&dev->dma_bo_lock);

   if (!p_atomic_dec_zero(&bo->refcnt)) {
      u_rwlock_rdunlock(&dev->dma_bo_lock);
      return;
   }

   tu_debug_bos_del(dev, bo);
   tu_dump_bo_del(dev, bo);

   if (bo->map) {
      TU_RMV(bo_unmap, dev, bo);
      munmap(bo->map, bo->size);
   }

   TU_RMV(bo_destroy, dev, bo);

   if (dev->physical_device->has_vm_bind) {
      if (!bo->lazy) {
         tu_map_vm_bind(dev, MSM_VM_BIND_OP_UNMAP, 0, bo->iova, 0, 0,
                        bo->size);
         tu_free_iova(dev, bo->iova, bo->size);
      }

      msm_bo_gem_close(dev, bo);
   } else if (dev->physical_device->has_set_iova) {
      tu_bo_list_del(dev, bo);
      tu_bo_make_zombie(dev, bo);
   } else {
      tu_bo_list_del(dev, bo);

      msm_bo_gem_close(dev, bo);
   }

   u_rwlock_rdunlock(&dev->dma_bo_lock);
}

static VkResult
msm_sparse_vma_init(struct tu_device *dev,
                    struct vk_object_base *base,
                    struct tu_sparse_vma *out_vma,
                    uint64_t *out_iova,
                    enum tu_sparse_vma_flags flags,
                    uint64_t size, uint64_t client_iova)
{
   VkResult result;
   enum tu_bo_alloc_flags bo_flags =
      (flags & TU_SPARSE_VMA_REPLAYABLE) ? TU_BO_ALLOC_REPLAYABLE :
      (enum tu_bo_alloc_flags)0;

   out_vma->msm.size = size;

   result = tu_allocate_iova(dev, 0, size, client_iova, bo_flags,
                             &out_vma->msm.iova);

   if (result != VK_SUCCESS)
      return result;

   if (flags & TU_SPARSE_VMA_MAP_ZERO) {
      assert(dev->physical_device->has_vm_bind);
      result = tu_map_vm_bind(dev, MSM_VM_BIND_OP_MAP_NULL, 0,
                              out_vma->msm.iova, 0, 0, size);
   }

   *out_iova = out_vma->msm.iova;

   return result;
}

static void
msm_sparse_vma_finish(struct tu_device *dev,
                      struct tu_sparse_vma *vma)
{
   if (dev->physical_device->has_vm_bind) {
      tu_map_vm_bind(dev, MSM_VM_BIND_OP_UNMAP, 0, vma->msm.iova, 0, 0,
                     vma->msm.size);
   }

   /* For has_set_iova, if a lazy BO was mapped into this sparse VMA
    * the allocation will be handed off to the zombie VMA mechanism.
    */
   if (dev->physical_device->has_vm_bind || !vma->msm.backs_lazy_bo)
      tu_free_iova(dev, vma->msm.iova, vma->msm.size);
}

static int
compare_binds(const void *_a, const void *_b)
{
   const struct drm_msm_vm_bind_op *a =
      (const struct drm_msm_vm_bind_op *)_a;
   const struct drm_msm_vm_bind_op *b =
      (const struct drm_msm_vm_bind_op *)_b;

   if (a->iova < b->iova)
      return -1;
   else if (a->iova > b->iova)
      return 1;
   else
      return 0;
}

static VkResult
msm_queue_submit(struct tu_queue *queue, void *_submit,
                 struct vk_sync_wait *waits, uint32_t wait_count,
                 struct vk_sync_signal *signals, uint32_t signal_count,
                 struct tu_u_trace_submission_data *u_trace_submission_data)
{
   VkResult result = VK_SUCCESS;
   int ret;
   struct tu_msm_queue_submit *submit =
      (struct tu_msm_queue_submit *)_submit;

   struct drm_msm_syncobj *in_syncobjs, *out_syncobjs;
   uint64_t gpu_offset = 0;
   uint32_t entry_count =
      util_dynarray_num_elements(&submit->commands, struct drm_msm_gem_submit_cmd);
   bool has_vm_bind = queue->device->physical_device->has_vm_bind;
#if HAVE_PERFETTO
   struct tu_perfetto_clocks clocks;
   uint64_t start_ts = tu_perfetto_begin_submit();
#endif
   uint32_t fence = 0;
   uint64_t other_timepoint =
      submit->binds.size != 0 ? queue->gfx_timepoint :
      queue->sparse_timepoint;
   uint32_t extra_wait_count =
      (queue->type == TU_QUEUE_GFX && other_timepoint != 0) ? 1 : 0;
   uint32_t extra_signal_count =
      (queue->type == TU_QUEUE_GFX && has_vm_bind) ? 1 : 0;

   /* Allocate without wait timeline semaphores */
   in_syncobjs = (struct drm_msm_syncobj *) vk_zalloc(
      &queue->device->vk.alloc,
      (wait_count + extra_wait_count) * sizeof(*in_syncobjs), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (in_syncobjs == NULL) {
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_in_syncobjs;
   }

   /* Allocate with signal timeline semaphores considered */
   out_syncobjs = (struct drm_msm_syncobj *) vk_zalloc(
      &queue->device->vk.alloc,
      (signal_count + extra_signal_count) * sizeof(*out_syncobjs), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (out_syncobjs == NULL) {
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_out_syncobjs;
   }

   for (uint32_t i = 0; i < wait_count; i++) {
      struct vk_sync *sync = waits[i].sync;

      in_syncobjs[i] = (struct drm_msm_syncobj) {
         .handle = vk_sync_as_drm_syncobj(sync)->syncobj,
         .flags = 0,
         .point = waits[i].wait_value,
      };
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      struct vk_sync *sync = signals[i].sync;

      out_syncobjs[i] = (struct drm_msm_syncobj) {
         .handle = vk_sync_as_drm_syncobj(sync)->syncobj,
         .flags = 0,
         .point = signals[i].signal_value,
      };
   }

   if (queue->type == TU_QUEUE_SPARSE || submit->binds.size != 0) {
      if (extra_wait_count) {
         in_syncobjs[wait_count] = (struct drm_msm_syncobj) {
            .handle = queue->gfx_syncobj,
            .flags = 0,
            .point = queue->gfx_timepoint,
         };
      }
      if (extra_signal_count) {
         out_syncobjs[signal_count] = (struct drm_msm_syncobj) {
            .handle = queue->sparse_syncobj,
            .flags = 0,
            .point = ++queue->sparse_timepoint,
         };
      }

      unsigned nr_ops = util_dynarray_num_elements(&submit->binds,
                                                   struct drm_msm_vm_bind_op);

      uint32_t flags = 0;

      /* The kernel needs to pre-allocate page table memory for bind
       * operations. It tries to estimate how much memory is needed, but if
       * the iova ranges to map aren't contiguous (i.e. if the end of one
       * mapping does not equal the start of the next) then it can
       * overestimate. Due to how we have to swizzle sparse image mappings, we
       * may map contiguous iova ranges from neighboring sparse tiles with
       * bind_op's that aren't next to each other in the ops array, resulting
       * in no mappings being contiguous and the kernel wildly overestimating
       * the memory required for page tables. Sort the entries to make sure
       * that neighboring mappings are next to each other.
       */
      qsort(submit->binds.data, nr_ops, sizeof(struct drm_msm_vm_bind_op),
            compare_binds);

      u_rwlock_rdlock(&queue->device->vm_bind_fence_lock);

      if (queue->device->vm_bind_fence_fd != -1)
         flags |= MSM_VM_BIND_FENCE_FD_IN;

      struct drm_msm_vm_bind req = {
         .flags = flags,
         .nr_ops = nr_ops,
         .fence_fd = queue->device->vm_bind_fence_fd,
         .queue_id = queue->sparse_queue_id,
         .in_syncobjs = (uint64_t)(uintptr_t)in_syncobjs,
         .out_syncobjs = (uint64_t)(uintptr_t)out_syncobjs,
         .nr_in_syncobjs = wait_count + extra_wait_count,
         .nr_out_syncobjs = signal_count + extra_signal_count,
         .syncobj_stride = sizeof(struct drm_msm_syncobj),
         .op_stride = sizeof(struct drm_msm_vm_bind_op),
      };

      /* If there's a single op, then it's inlined into the request struct
       * instead of being provided as a pointer.
       */
      if (req.nr_ops == 1) {
         memcpy(&req.op, submit->binds.data, sizeof(req.op));
      } else {
         req.ops = (uint64_t)(uintptr_t)submit->binds.data;
      }

      {
         MESA_TRACE_SCOPE("DRM_MSM_VM_BIND");
         ret = drmCommandWriteRead(queue->device->fd,
                                 DRM_MSM_VM_BIND,
                                 &req, sizeof(req));
      }
      int errno_ = errno;

      u_rwlock_rdunlock(&queue->device->vm_bind_fence_lock);

      if (ret) {
         assert(errno_ != EINVAL);
         if (errno == ENOMEM) {
            MESA_TRACE_SCOPE("DRM_MSM_VM_BIND OOM path");

            perf_debug(queue->device,
                       "Falling back for sparse binding due to kernel OOM");

            /* The kernel ran out of memory allocating memory for the bind
             * objects. Wait for the syncobjs manually, so that the kernel can
             * complete each command and free its associated
             * memory immediately, and then submit one map at a time.
             */
            result = vk_sync_wait_many(&queue->device->vk,
                                       wait_count, waits,
                                       VK_SYNC_WAIT_COMPLETE, INT64_MAX);
            if (result != VK_SUCCESS) {
               result = vk_device_set_lost(&queue->device->vk,
                                           "vk_sync_wait_many failed");
               goto fail_submit;
            }

            uint32_t flags = 0;

            u_rwlock_rdlock(&queue->device->vm_bind_fence_lock);

            if (queue->device->vm_bind_fence_fd != -1)
               flags |= MSM_VM_BIND_FENCE_FD_IN;

            util_dynarray_foreach (&submit->binds, struct drm_msm_vm_bind_op,
                                   op) {
               bool last =
                  op == util_dynarray_top_ptr(&submit->binds,
                                              struct drm_msm_vm_bind_op);
               struct drm_msm_vm_bind req = {
                  .flags = flags,
                  .nr_ops = 1,
                  .fence_fd = queue->device->vm_bind_fence_fd,
                  .queue_id = queue->sparse_queue_id,
                  .in_syncobjs =
                     (uint64_t)(uintptr_t)&in_syncobjs[wait_count],
                  .out_syncobjs = (uint64_t)(uintptr_t)out_syncobjs,
                  .nr_in_syncobjs = extra_wait_count,
                  .nr_out_syncobjs = last ? signal_count +
                     extra_signal_count : 0,
                  .syncobj_stride = sizeof(struct drm_msm_syncobj),
                  .op_stride = sizeof(struct drm_msm_vm_bind_op),
                  .op = *op,
               };

               {
                  MESA_TRACE_SCOPE("DRM_MSM_VM_BIND");
                  ret = drmCommandWriteRead(queue->device->fd,
                                          DRM_MSM_VM_BIND,
                                          &req, sizeof(req));
               }

               if (ret)
                  break;
            }

            u_rwlock_rdunlock(&queue->device->vm_bind_fence_lock);
         }
      }
   } else {
      uint32_t flags = MSM_PIPE_3D0;

      if (wait_count)
         flags |= MSM_SUBMIT_SYNCOBJ_IN;

      if (signal_count)
         flags |= MSM_SUBMIT_SYNCOBJ_OUT;

      if (has_vm_bind) {
         if (extra_wait_count) {
            in_syncobjs[wait_count] = (struct drm_msm_syncobj) {
               .handle = queue->sparse_syncobj,
               .flags = 0,
               .point = queue->sparse_timepoint,
            };
         }
         if (extra_signal_count) {
            out_syncobjs[signal_count] = (struct drm_msm_syncobj) {
               .handle = queue->gfx_syncobj,
               .flags = 0,
               .point = ++queue->gfx_timepoint,
            };
         }

         u_rwlock_rdlock(&queue->device->vm_bind_fence_lock);

         if (queue->device->vm_bind_fence_fd != -1)
            flags |= MSM_SUBMIT_FENCE_FD_IN;
      } else {
         mtx_lock(&queue->device->bo_mutex);

         /* MSM_SUBMIT_NO_IMPLICIT skips having the scheduler wait on the
          * previous dma fences attached to the BO (such as from the window
          * system server's command queue) before submitting the job. Our
          * fence will always get attached to the BO, because it gets used for
          * synchronization for the shrinker.
          *
          * If the flag is not set, then the kernel falls back to checking
          * each BO's MSM_SUBMIT_NO_IMPLICIT flag for its implicit sync
          * handling.
          *
          * As of kernel 6.0, the core wsi code will be generating appropriate
          * syncobj export-and-waits/signal-and-imports for implict syncing
          * (on implicit sync WSI backends) and not allocating any
          * wsi_memory_allocate_info->implicit_sync BOs from the driver.
          * However, on older kernels with that flag set, we have to submit
          * without NO_IMPLICIT set to do have the kernel do pre-submit waits
          * on whatever the last fence was.
          */
         if (queue->device->implicit_sync_bo_count == 0)
            flags |= MSM_SUBMIT_NO_IMPLICIT;

         /* drm_msm_gem_submit_cmd requires index of bo which could change at
          * any time when bo_mutex is not locked. So we update the index here
          * under the lock.
          */
         util_dynarray_foreach (&submit->commands, struct drm_msm_gem_submit_cmd,
                                cmd) {
            unsigned i = cmd -
               util_dynarray_element(&submit->commands,
                                     struct drm_msm_gem_submit_cmd, 0);
            struct tu_bo **bo = util_dynarray_element(&submit->command_bos,
                                                      struct tu_bo *, i);
            cmd->submit_idx = (*bo)->submit_bo_list_idx;
         }
      }

      struct drm_msm_gem_submit req = {
         .flags = flags,
         .nr_bos = entry_count ? queue->device->submit_bo_count : 0,
         .nr_cmds = entry_count,
         .bos = (uint64_t)(uintptr_t) queue->device->submit_bo_list,
         .cmds = (uint64_t)(uintptr_t)submit->commands.data,
         .fence_fd = queue->device->vm_bind_fence_fd,
         .queueid = queue->msm_queue_id,
         .in_syncobjs = (uint64_t)(uintptr_t)in_syncobjs,
         .out_syncobjs = (uint64_t)(uintptr_t)out_syncobjs,
         .nr_in_syncobjs = wait_count + extra_wait_count,
         .nr_out_syncobjs = signal_count + extra_signal_count,
         .syncobj_stride = sizeof(struct drm_msm_syncobj),
      };

      {
         MESA_TRACE_SCOPE("DRM_MSM_GEM_SUBMIT");
         ret = drmCommandWriteRead(queue->device->fd,
                                 DRM_MSM_GEM_SUBMIT,
                                 &req, sizeof(req));
      }

      if (has_vm_bind)
         u_rwlock_rdunlock(&queue->device->vm_bind_fence_lock);
      else
         mtx_unlock(&queue->device->bo_mutex);

      fence = req.fence;
   }

   if (ret) {
      result = vk_device_set_lost(&queue->device->vk, "submit failed: %m");
      goto fail_submit;
   }

   if (queue->type != TU_QUEUE_SPARSE)
      p_atomic_set(&queue->fence, fence);

#if HAVE_PERFETTO
   clocks = tu_perfetto_end_submit(queue, queue->device->submit_count,
                                   start_ts, NULL);
   gpu_offset = clocks.gpu_ts_offset;
#endif

   if (u_trace_submission_data) {
      u_trace_submission_data->gpu_ts_offset = gpu_offset;
   }

fail_submit:
   vk_free(&queue->device->vk.alloc, out_syncobjs);
fail_out_syncobjs:
   vk_free(&queue->device->vk.alloc, in_syncobjs);
fail_in_syncobjs:
   return result;
}

static const struct tu_knl msm_knl_funcs = {
      .name = "msm",

      .device_init = msm_device_init,
      .device_finish = msm_device_finish,
      .device_get_gpu_timestamp = msm_device_get_gpu_timestamp,
      .device_get_suspend_count = msm_device_get_suspend_count,
      .device_check_status = msm_device_check_status,
      .submitqueue_new = msm_submitqueue_new,
      .submitqueue_close = msm_submitqueue_close,
      .bo_init = msm_bo_init,
      .bo_init_dmabuf = msm_bo_init_dmabuf,
      .bo_export_dmabuf = tu_drm_export_dmabuf,
      .bo_map = msm_bo_map,
      .bo_allow_dump = msm_bo_allow_dump,
      .bo_finish = msm_bo_finish,
      .bo_set_metadata = msm_bo_set_metadata,
      .bo_get_metadata = msm_bo_get_metadata,
      .submit_create = msm_submit_create,
      .submit_finish = msm_submit_finish,
      .submit_add_entries = msm_submit_add_entries,
      .submit_add_bind = msm_submit_add_bind,
      .queue_submit = msm_queue_submit,
      .queue_wait_fence = msm_queue_wait_fence,
      .sparse_vma_init = msm_sparse_vma_init,
      .sparse_vma_finish = msm_sparse_vma_finish,
};

VkResult
tu_knl_drm_msm_load(struct tu_instance *instance,
                    int fd, struct _drmVersion *version,
                    struct tu_physical_device **out)
{
   VkResult result = VK_SUCCESS;

   /* Version 1.6 added SYNCOBJ support. */
   const int min_version_major = 1;
   const int min_version_minor = 6;

   if (version->version_major != min_version_major ||
       version->version_minor < min_version_minor) {
      result = vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                                 "kernel driver for device %s has version %d.%d, "
                                 "but Vulkan requires version >= %d.%d",
                                 version->name,
                                 version->version_major, version->version_minor,
                                 min_version_major, min_version_minor);
      return result;
   }

   struct tu_physical_device *device = (struct tu_physical_device *)
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }

   device->msm_major_version = version->version_major;
   device->msm_minor_version = version->version_minor;

   device->instance = instance;
   device->local_fd = fd;

   device->has_vm_bind = tu_try_enable_vm_bind(fd) == 0;
   device->has_sparse = device->has_vm_bind;

   if (tu_drm_get_gpu_id(device, &device->dev_id.gpu_id)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get GPU ID");
      goto fail;
   }

   if (tu_drm_get_param(fd, MSM_PARAM_CHIP_ID, &device->dev_id.chip_id)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get CHIP ID");
      goto fail;
   }

   if (tu_drm_get_gmem_size(device, &device->gmem_size)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                "could not get GMEM size");
      goto fail;
   }
   device->gmem_size = debug_get_num_option("TU_GMEM", device->gmem_size);

   if (tu_drm_get_gmem_base(device, &device->gmem_base)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get GMEM size");
      goto fail;
   }

   device->has_set_iova = !tu_drm_get_va_prop(device, &device->va_start,
                                              &device->va_size);
   device->has_lazy_bos = device->has_set_iova;
   device->has_raytracing = tu_drm_get_raytracing(device);
   device->has_sparse_prr = tu_drm_get_prr(device);

   device->has_preemption = tu_drm_has_preemption(device);

   /* Even if kernel is new enough, the GPU itself may not support it. */
   device->has_cached_coherent_memory =
      (device->msm_minor_version >= 8) &&
      tu_drm_is_memory_type_supported(fd, MSM_BO_CACHED_COHERENT);

   tu_drm_set_debuginfo(fd);

   device->submitqueue_priority_count = tu_drm_get_priorities(device);

   device->ubwc_config.highest_bank_bit = tu_drm_get_highest_bank_bit(device);
   device->ubwc_config.bank_swizzle_levels = tu_drm_get_ubwc_swizzle(device);
   device->ubwc_config.macrotile_mode = tu_drm_get_macrotile_mode(device);

   device->uche_trap_base = tu_drm_get_uche_trap_base(device);

   device->syncobj_type = vk_drm_syncobj_get_type(fd);

   /* msm didn't expose DRM_CAP_SYNCOBJ_TIMELINE until kernel 6.15, so emulate timeline
    * semaphores if necessary.
    */
   if (!(device->syncobj_type.features & VK_SYNC_FEATURE_TIMELINE))
      device->timeline_type = vk_sync_timeline_get_type(&device->syncobj_type);

   device->sync_types[0] = &device->syncobj_type;
   device->sync_types[1] = &device->timeline_type.sync;
   device->sync_types[2] = NULL;

   device->heap.size = tu_get_system_heap_size(device);
   device->heap.used = 0u;
   device->heap.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   instance->knl = &msm_knl_funcs;

   *out = device;

   return VK_SUCCESS;

fail:
   vk_free(&instance->vk.alloc, device);
   return result;
}
