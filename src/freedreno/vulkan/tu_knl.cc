/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include <fcntl.h>

#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#include <sys/mman.h>

#include "vk_debug_utils.h"

#include "util/cache_ops.h"
#include "util/libdrm.h"

#include "tu_device.h"
#include "tu_knl.h"
#include "tu_queue.h"
#include "tu_rmv.h"


VkResult
tu_bo_init_new_explicit_iova(struct tu_device *dev,
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
   struct tu_instance *instance = dev->physical_device->instance;

   size = align64(size, os_page_size);

   VkResult result =
      dev->instance->knl->bo_init(dev, base, out_bo, size, client_iova,
                                  mem_property, flags, lazy_vma, name);
   if (result != VK_SUCCESS)
      return result;

   if ((mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
       !(mem_property & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      (*out_bo)->cached_non_coherent = true;

   vk_address_binding_report(&instance->vk, base ? base : &dev->vk.base,
                             (*out_bo)->iova, (*out_bo)->size,
                             VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   (*out_bo)->dump = flags & TU_BO_ALLOC_ALLOW_DUMP;

   return VK_SUCCESS;
}

VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo **bo,
                  uint64_t size,
                  int fd)
{
   size = align64(size, os_page_size);
   VkResult result = dev->instance->knl->bo_init_dmabuf(dev, bo, size, fd);
   if (result != VK_SUCCESS)
      return result;

   /* If we have non-coherent cached memory, then defensively assume that it
    * may need to be invalidated/flushed. If not, then we just have to assume
    * that whatever dma-buf producer didn't allocate it non-coherent cached
    * because we have no way of handling that.
    */
   if (dev->physical_device->has_cached_non_coherent_memory)
      (*bo)->cached_non_coherent = true;

   return VK_SUCCESS;
}

int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo)
{
   return dev->instance->knl->bo_export_dmabuf(dev, bo);
}

void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo)
{
   MESA_TRACE_FUNC();
   struct tu_instance *instance = dev->physical_device->instance;

   vk_address_binding_report(&instance->vk, bo->base ? bo->base : &dev->vk.base,
                             bo->iova, bo->size,
                             VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   dev->instance->knl->bo_finish(dev, bo);
}

VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo, void *placed_addr)
{
   if (bo->map && (placed_addr == NULL || placed_addr == bo->map))
      return VK_SUCCESS;
   else if (bo->map)
      /* The BO is already mapped, but with a different address. */
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED, "Cannot remap BO to a different address");

   return dev->instance->knl->bo_map(dev, bo, placed_addr);
}

VkResult
tu_bo_unmap(struct tu_device *dev, struct tu_bo *bo, bool reserve)
{
   if (!bo->map || bo->never_unmap)
      return VK_SUCCESS;

   TU_RMV(bo_unmap, dev, bo);

   if (reserve) {
      void *map = mmap(bo->map, bo->size, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
      if (map == MAP_FAILED)
         return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                          "Failed to replace mapping with reserved memory");
   } else {
      munmap(bo->map, bo->size);
   }

   bo->map = NULL;

   return VK_SUCCESS;
}

void
tu_bo_sync_cache(struct tu_device *dev,
                 struct tu_bo *bo,
                 VkDeviceSize offset,
                 VkDeviceSize size,
                 enum tu_mem_sync_op op)
{
   char *start = (char *) bo->map + offset;

   size = size == VK_WHOLE_SIZE ? (bo->size - offset) : size;
   if (op == TU_MEM_SYNC_CACHE_TO_GPU) {
      util_flush_range(start, size);
   } else {
      util_flush_inval_range(start, size);
   }
}

void tu_bo_allow_dump(struct tu_device *dev, struct tu_bo *bo)
{
   dev->instance->knl->bo_allow_dump(dev, bo);

   p_atomic_set(&bo->dump, true);
}

void
tu_bo_set_metadata(struct tu_device *dev, struct tu_bo *bo,
                   void *metadata, uint32_t metadata_size)
{
   if (!dev->instance->knl->bo_set_metadata)
      return;
   dev->instance->knl->bo_set_metadata(dev, bo, metadata, metadata_size);
}

VkResult
tu_sparse_vma_init(struct tu_device *dev,
                   struct vk_object_base *base,
                   struct tu_sparse_vma *out_vma,
                   uint64_t *out_iova,
                   enum tu_sparse_vma_flags flags,
                   uint64_t size, uint64_t client_iova)
{
   size = align64(size, os_page_size);

   out_vma->flags = flags;
   return dev->instance->knl->sparse_vma_init(dev, base, out_vma, out_iova,
                                              flags, size, client_iova);

}

void
tu_sparse_vma_finish(struct tu_device *dev,
                     struct tu_sparse_vma *vma)
{
   dev->instance->knl->sparse_vma_finish(dev, vma);
}

int
tu_bo_get_metadata(struct tu_device *dev, struct tu_bo *bo,
                   void *metadata, uint32_t metadata_size)
{
   if (!dev->instance->knl->bo_get_metadata)
      return -ENOSYS;
   return dev->instance->knl->bo_get_metadata(dev, bo, metadata, metadata_size);
}

VkResult
tu_drm_device_init(struct tu_device *dev)
{
   return dev->instance->knl->device_init(dev);
}

void
tu_drm_device_finish(struct tu_device *dev)
{
   dev->instance->knl->device_finish(dev);
}

int
tu_device_get_gpu_timestamp(struct tu_device *dev,
                            uint64_t *ts)
{
   return dev->instance->knl->device_get_gpu_timestamp(dev, ts);
}

int
tu_device_get_suspend_count(struct tu_device *dev,
                            uint64_t *suspend_count)
{
   return dev->instance->knl->device_get_suspend_count(dev, suspend_count);
}

VkResult
tu_queue_wait_fence(struct tu_queue *queue, uint32_t fence,
                    uint64_t timeout_ns)
{
   return queue->device->instance->knl->queue_wait_fence(queue, fence,
                                                         timeout_ns);
}

VkResult
tu_device_check_status(struct vk_device *vk_device)
{
   struct tu_device *dev = container_of(vk_device, struct tu_device, vk);
   return dev->instance->knl->device_check_status(dev);
}

int
tu_drm_submitqueue_new(struct tu_device *dev, struct tu_queue *queue)
{
   return dev->instance->knl->submitqueue_new(dev, queue);
}

void
tu_drm_submitqueue_close(struct tu_device *dev, struct tu_queue *queue)
{
   dev->instance->knl->submitqueue_close(dev, queue);
}

void *
tu_submit_create(struct tu_device *dev)
{
   return dev->instance->knl->submit_create(dev);
}

void
tu_submit_finish(struct tu_device *dev, void *submit)
{
   return dev->instance->knl->submit_finish(dev, submit);
}

void
tu_submit_add_entries(struct tu_device *dev, void *submit,
                      struct tu_cs_entry *entries,
                      unsigned num_entries)
{
   return dev->instance->knl->submit_add_entries(dev, submit, entries,
                                                 num_entries);
}

void
tu_submit_add_bind(struct tu_device *dev,
                   void *_submit,
                   struct tu_sparse_vma *vma, uint64_t vma_offset,
                   struct tu_bo *bo, uint64_t bo_offset,
                   uint64_t size)
{
   assert(vma_offset % 4096 == 0);
   assert(bo_offset % 4096 == 0);
   return dev->instance->knl->submit_add_bind(dev, _submit, vma, vma_offset,
                                              bo, bo_offset, size);
}

VkResult
tu_queue_submit(struct tu_queue *queue, void *submit,
                struct vk_sync_wait *waits, uint32_t wait_count,
                struct vk_sync_signal *signals, uint32_t signal_count,
                struct tu_u_trace_submission_data *u_trace_submission_data)
{
   return queue->device->instance->knl->queue_submit(queue, submit,
                                                     waits, wait_count,
                                                     signals, signal_count,
                                                     u_trace_submission_data);
}

/**
 * Enumeration entrypoint specific to non-drm devices (ie. kgsl)
 */
VkResult
tu_enumerate_devices(struct vk_instance *vk_instance)
{
#ifdef TU_HAS_KGSL
   struct tu_instance *instance =
      container_of(vk_instance, struct tu_instance, vk);

   static const char path[] = "/dev/kgsl-3d0";
   int fd;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      if (errno == ENOENT)
         return VK_ERROR_INCOMPATIBLE_DRIVER;

      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "failed to open device %s", path);
   }

   VkResult result = tu_knl_kgsl_load(instance, fd);
   if (result != VK_SUCCESS) {
      close(fd);
      return result;
   }

   if (TU_DEBUG(STARTUP))
      mesa_logi("Found compatible device '%s'.", path);

   return result;
#else
   return VK_ERROR_INCOMPATIBLE_DRIVER;
#endif
}

/**
 * Enumeration entrypoint for drm devices
 */
VkResult
tu_physical_device_try_create(struct vk_instance *vk_instance,
                              struct _drmDevice *drm_device,
                              struct vk_physical_device **out)
{
   struct tu_instance *instance =
      container_of(vk_instance, struct tu_instance, vk);

   /* Note that "msm" is a platform device, but "virtio_gpu" is a pci
    * device.  In general we shouldn't care about the bus type.
    */
   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER)))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   const char *primary_path = drm_device->nodes[DRM_NODE_PRIMARY];
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   drmVersionPtr version;
   int fd;
   int master_fd = -1;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "failed to open device %s", path);
   }

   version = drmGetVersion(fd);
   if (!version) {
      close(fd);
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "failed to query kernel driver version for device %s",
                               path);
   }

   struct tu_physical_device *device = NULL;

   VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;

#ifdef TU_HAS_VIRTIO
   if (debug_get_bool_option("FD_FORCE_VTEST", false)) {
      result = tu_knl_drm_virtio_load(instance, -1, version, &device);
      path = "";
   } else
#endif
   if (strcmp(version->name, "msm") == 0) {
#ifdef TU_HAS_MSM
      result = tu_knl_drm_msm_load(instance, fd, version, &device);
#endif
   } else if (strcmp(version->name, "virtio_gpu") == 0) {
#ifdef TU_HAS_VIRTIO
      result = tu_knl_drm_virtio_load(instance, fd, version, &device);
#endif
   } else if (TU_DEBUG(STARTUP)) {
      result = vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                                 "device %s (%s) is not compatible with turnip",
                                 path, version->name);
   }

   if (result != VK_SUCCESS)
      goto out;

   assert(device);

   if (instance->vk.enabled_extensions.KHR_display) {
      master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
   }

   device->master_fd = master_fd;
   device->kgsl_dma_fd = -1;

   assert(strlen(path) < ARRAY_SIZE(device->fd_path));
   snprintf(device->fd_path, ARRAY_SIZE(device->fd_path), "%s", path);

   struct stat st;

   if (stat(primary_path, &st) == 0) {
      device->has_master = true;
      device->master_major = major(st.st_rdev);
      device->master_minor = minor(st.st_rdev);
   } else {
      device->has_master = false;
      device->master_major = 0;
      device->master_minor = 0;
   }

   if (strlen(path) == 0) {
      /* if vtest, then fake it: */
      device->has_local = true;
      device->local_major = 226;
      device->local_minor = 128;
   } else if (stat(path, &st) == 0) {
      device->has_local = true;
      device->local_major = major(st.st_rdev);
      device->local_minor = minor(st.st_rdev);
   } else {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "failed to stat DRM render node %s", path);
      goto out;
   }

   result = tu_physical_device_init(device, instance);
   if (result != VK_SUCCESS)
      goto out;

   if (TU_DEBUG(STARTUP))
      mesa_logi("Found compatible device '%s' (%s).", path, version->name);

   *out = &device->vk;

out:
   if (result != VK_SUCCESS) {
      if (master_fd != -1)
         close(master_fd);
      close(fd);
      vk_free(&instance->vk.alloc, device);
   }

   drmFreeVersion(version);

   return result;
}
