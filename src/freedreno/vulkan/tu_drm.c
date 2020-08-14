/*
 * Copyright © 2018 Google, Inc.
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "vk_util.h"

#include "drm-uapi/msm_drm.h"

#include "tu_private.h"

static int
tu_drm_get_param(const struct tu_physical_device *dev,
                 uint32_t param,
                 uint64_t *value)
{
   /* Technically this requires a pipe, but the kernel only supports one pipe
    * anyway at the time of writing and most of these are clearly pipe
    * independent. */
   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
   };

   int ret = drmCommandWriteRead(dev->local_fd, DRM_MSM_GET_PARAM, &req,
                                 sizeof(req));
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

static int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev, MSM_PARAM_GPU_ID, &value);
   if (ret)
      return ret;

   *id = value;
   return 0;
}

static int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev, MSM_PARAM_GMEM_SIZE, &value);
   if (ret)
      return ret;

   *size = value;
   return 0;
}

static int
tu_drm_get_gmem_base(const struct tu_physical_device *dev, uint64_t *base)
{
   return tu_drm_get_param(dev, MSM_PARAM_GMEM_BASE, base);
}

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id)
{
   struct drm_msm_submitqueue req = {
      .flags = 0,
      .prio = priority,
   };

   int ret = drmCommandWriteRead(dev->physical_device->local_fd,
                                 DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
   if (ret)
      return ret;

   *queue_id = req.id;
   return 0;
}

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id)
{
   drmCommandWrite(dev->physical_device->local_fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                   &queue_id, sizeof(uint32_t));
}

static void
tu_gem_close(const struct tu_device *dev, uint32_t gem_handle)
{
   struct drm_gem_close req = {
      .handle = gem_handle,
   };

   drmIoctl(dev->physical_device->local_fd, DRM_IOCTL_GEM_CLOSE, &req);
}

/** Helper for DRM_MSM_GEM_INFO, returns 0 on error. */
static uint64_t
tu_gem_info(const struct tu_device *dev, uint32_t gem_handle, uint32_t info)
{
   struct drm_msm_gem_info req = {
      .handle = gem_handle,
      .info = info,
   };

   int ret = drmCommandWriteRead(dev->physical_device->local_fd,
                                 DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret < 0)
      return 0;

   return req.value;
}

static VkResult
tu_bo_init(struct tu_device *dev,
           struct tu_bo *bo,
           uint32_t gem_handle,
           uint64_t size)
{
   uint64_t iova = tu_gem_info(dev, gem_handle, MSM_INFO_GET_IOVA);
   if (!iova) {
      tu_gem_close(dev, gem_handle);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   *bo = (struct tu_bo) {
      .gem_handle = gem_handle,
      .size = size,
      .iova = iova,
   };

   return VK_SUCCESS;
}

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size)
{
   /* TODO: Choose better flags. As of 2018-11-12, freedreno/drm/msm_bo.c
    * always sets `flags = MSM_BO_WC`, and we copy that behavior here.
    */
   struct drm_msm_gem_new req = {
      .size = size,
      .flags = MSM_BO_WC
   };

   int ret = drmCommandWriteRead(dev->physical_device->local_fd,
                                 DRM_MSM_GEM_NEW, &req, sizeof(req));
   if (ret)
      return vk_error(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return tu_bo_init(dev, bo, req.handle, size);
}

VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo *bo,
                  uint64_t size,
                  int prime_fd)
{
   /* lseek() to get the real size */
   off_t real_size = lseek(prime_fd, 0, SEEK_END);
   lseek(prime_fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size)
      return vk_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   uint32_t gem_handle;
   int ret = drmPrimeFDToHandle(dev->physical_device->local_fd, prime_fd,
                                &gem_handle);
   if (ret)
      return vk_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   return tu_bo_init(dev, bo, gem_handle, size);
}

int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo)
{
   int prime_fd;
   int ret = drmPrimeHandleToFD(dev->physical_device->local_fd, bo->gem_handle,
                                DRM_CLOEXEC, &prime_fd);

   return ret == 0 ? prime_fd : -1;
}

VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo)
{
   if (bo->map)
      return VK_SUCCESS;

   uint64_t offset = tu_gem_info(dev, bo->gem_handle, MSM_INFO_GET_OFFSET);
   if (!offset)
      return vk_error(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   /* TODO: Should we use the wrapper os_mmap() like Freedreno does? */
   void *map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    dev->physical_device->local_fd, offset);
   if (map == MAP_FAILED)
      return vk_error(dev->instance, VK_ERROR_MEMORY_MAP_FAILED);

   bo->map = map;
   return VK_SUCCESS;
}

void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo)
{
   assert(bo->gem_handle);

   if (bo->map)
      munmap(bo->map, bo->size);

   tu_gem_close(dev, bo->gem_handle);
}

static VkResult
tu_drm_device_init(struct tu_physical_device *device,
                   struct tu_instance *instance,
                   drmDevicePtr drm_device)
{
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   VkResult result = VK_SUCCESS;
   drmVersionPtr version;
   int fd;
   int master_fd = -1;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "failed to open device %s", path);
   }

   /* Version 1.3 added MSM_INFO_IOVA. */
   const int min_version_major = 1;
   const int min_version_minor = 3;

   version = drmGetVersion(fd);
   if (!version) {
      close(fd);
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "failed to query kernel driver version for device %s",
                       path);
   }

   if (strcmp(version->name, "msm")) {
      drmFreeVersion(version);
      close(fd);
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "device %s does not use the msm kernel driver", path);
   }

   if (version->version_major != min_version_major ||
       version->version_minor < min_version_minor) {
      result = vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                         "kernel driver for device %s has version %d.%d, "
                         "but Vulkan requires version >= %d.%d",
                         path, version->version_major, version->version_minor,
                         min_version_major, min_version_minor);
      drmFreeVersion(version);
      close(fd);
      return result;
   }

   device->msm_major_version = version->version_major;
   device->msm_minor_version = version->version_minor;

   drmFreeVersion(version);

   if (instance->debug_flags & TU_DEBUG_STARTUP)
      tu_logi("Found compatible device '%s'.", path);

   vk_object_base_init(NULL, &device->base, VK_OBJECT_TYPE_PHYSICAL_DEVICE);
   device->instance = instance;
   assert(strlen(path) < ARRAY_SIZE(device->path));
   strncpy(device->path, path, ARRAY_SIZE(device->path));

   if (instance->enabled_extensions.KHR_display) {
      master_fd =
         open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         /* TODO: free master_fd is accel is not working? */
      }
   }

   device->master_fd = master_fd;
   device->local_fd = fd;

   if (tu_drm_get_gpu_id(device, &device->gpu_id)) {
      if (instance->debug_flags & TU_DEBUG_STARTUP)
         tu_logi("Could not query the GPU ID");
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "could not get GPU ID");
      goto fail;
   }

   if (tu_drm_get_gmem_size(device, &device->gmem_size)) {
      if (instance->debug_flags & TU_DEBUG_STARTUP)
         tu_logi("Could not query the GMEM size");
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "could not get GMEM size");
      goto fail;
   }

   if (tu_drm_get_gmem_base(device, &device->gmem_base)) {
      if (instance->debug_flags & TU_DEBUG_STARTUP)
         tu_logi("Could not query the GMEM size");
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "could not get GMEM size");
      goto fail;
   }

   return tu_physical_device_init(device, instance);

fail:
   close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

VkResult
tu_enumerate_devices(struct tu_instance *instance)
{
   /* TODO: Check for more devices ? */
   drmDevicePtr devices[8];
   VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;
   int max_devices;

   instance->physical_device_count = 0;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));

   if (instance->debug_flags & TU_DEBUG_STARTUP) {
      if (max_devices < 0)
         tu_logi("drmGetDevices2 returned error: %s\n", strerror(max_devices));
      else
         tu_logi("Found %d drm nodes", max_devices);
   }

   if (max_devices < 1)
      return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);

   for (unsigned i = 0; i < (unsigned) max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PLATFORM) {

         result = tu_drm_device_init(
            instance->physical_devices + instance->physical_device_count,
            instance, devices[i]);
         if (result == VK_SUCCESS)
            ++instance->physical_device_count;
         else if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
            break;
      }
   }
   drmFreeDevices(devices, max_devices);

   return result;
}

// Queue semaphore functions

static void
tu_semaphore_part_destroy(struct tu_device *device,
                          struct tu_semaphore_part *part)
{
   switch(part->kind) {
   case TU_SEMAPHORE_NONE:
      break;
   case TU_SEMAPHORE_SYNCOBJ:
      drmSyncobjDestroy(device->physical_device->local_fd, part->syncobj);
      break;
   }
   part->kind = TU_SEMAPHORE_NONE;
}

static void
tu_semaphore_remove_temp(struct tu_device *device,
                         struct tu_semaphore *sem)
{
   if (sem->temporary.kind != TU_SEMAPHORE_NONE) {
      tu_semaphore_part_destroy(device, &sem->temporary);
   }
}

static VkResult
tu_get_semaphore_syncobjs(const VkSemaphore *sems,
                          uint32_t sem_count,
                          bool wait,
                          struct drm_msm_gem_submit_syncobj **out,
                          uint32_t *out_count)
{
   uint32_t syncobj_count = 0;
   struct drm_msm_gem_submit_syncobj *syncobjs;

   for (uint32_t i = 0; i  < sem_count; ++i) {
      TU_FROM_HANDLE(tu_semaphore, sem, sems[i]);

      struct tu_semaphore_part *part =
         sem->temporary.kind != TU_SEMAPHORE_NONE ?
            &sem->temporary : &sem->permanent;

      if (part->kind == TU_SEMAPHORE_SYNCOBJ)
         ++syncobj_count;
   }

   *out = NULL;
   *out_count = syncobj_count;
   if (!syncobj_count)
      return VK_SUCCESS;

   *out = syncobjs = calloc(syncobj_count, sizeof (*syncobjs));
   if (!syncobjs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0, j = 0; i  < sem_count; ++i) {
      TU_FROM_HANDLE(tu_semaphore, sem, sems[i]);

      struct tu_semaphore_part *part =
         sem->temporary.kind != TU_SEMAPHORE_NONE ?
            &sem->temporary : &sem->permanent;

      if (part->kind == TU_SEMAPHORE_SYNCOBJ) {
         syncobjs[j].handle = part->syncobj;
         syncobjs[j].flags = wait ? MSM_SUBMIT_SYNCOBJ_RESET : 0;
         ++j;
      }
   }

   return VK_SUCCESS;
}

static void
tu_semaphores_remove_temp(struct tu_device *device,
                          const VkSemaphore *sems,
                          uint32_t sem_count)
{
   for (uint32_t i = 0; i  < sem_count; ++i) {
      TU_FROM_HANDLE(tu_semaphore, sem, sems[i]);
      tu_semaphore_remove_temp(device, sem);
   }
}

VkResult
tu_CreateSemaphore(VkDevice _device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   struct tu_semaphore *sem =
         vk_object_alloc(&device->vk, pAllocator, sizeof(*sem),
                         VK_OBJECT_TYPE_SEMAPHORE);
   if (!sem)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkExportSemaphoreCreateInfo *export =
      vk_find_struct_const(pCreateInfo->pNext, EXPORT_SEMAPHORE_CREATE_INFO);
   VkExternalSemaphoreHandleTypeFlags handleTypes =
      export ? export->handleTypes : 0;

   sem->permanent.kind = TU_SEMAPHORE_NONE;
   sem->temporary.kind = TU_SEMAPHORE_NONE;

   if (handleTypes) {
      if (drmSyncobjCreate(device->physical_device->local_fd, 0, &sem->permanent.syncobj) < 0) {
          vk_free2(&device->vk.alloc, pAllocator, sem);
          return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      sem->permanent.kind = TU_SEMAPHORE_SYNCOBJ;
   }
   *pSemaphore = tu_semaphore_to_handle(sem);
   return VK_SUCCESS;
}

void
tu_DestroySemaphore(VkDevice _device,
                    VkSemaphore _semaphore,
                    const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_semaphore, sem, _semaphore);
   if (!_semaphore)
      return;

   tu_semaphore_part_destroy(device, &sem->permanent);
   tu_semaphore_part_destroy(device, &sem->temporary);

   vk_object_free(&device->vk, pAllocator, sem);
}

VkResult
tu_ImportSemaphoreFdKHR(VkDevice _device,
                        const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_semaphore, sem, pImportSemaphoreFdInfo->semaphore);
   int ret;
   struct tu_semaphore_part *dst = NULL;

   if (pImportSemaphoreFdInfo->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT) {
      dst = &sem->temporary;
   } else {
      dst = &sem->permanent;
   }

   uint32_t syncobj = dst->kind == TU_SEMAPHORE_SYNCOBJ ? dst->syncobj : 0;

   switch(pImportSemaphoreFdInfo->handleType) {
      case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT: {
         uint32_t old_syncobj = syncobj;
         ret = drmSyncobjFDToHandle(device->physical_device->local_fd, pImportSemaphoreFdInfo->fd, &syncobj);
         if (ret == 0) {
            close(pImportSemaphoreFdInfo->fd);
            if (old_syncobj)
               drmSyncobjDestroy(device->physical_device->local_fd, old_syncobj);
         }
         break;
      }
      case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT: {
         if (!syncobj) {
            ret = drmSyncobjCreate(device->physical_device->local_fd, 0, &syncobj);
            if (ret)
               break;
         }
         if (pImportSemaphoreFdInfo->fd == -1) {
            ret = drmSyncobjSignal(device->physical_device->local_fd, &syncobj, 1);
         } else {
            ret = drmSyncobjImportSyncFile(device->physical_device->local_fd, syncobj, pImportSemaphoreFdInfo->fd);
         }
         if (!ret)
            close(pImportSemaphoreFdInfo->fd);
         break;
      }
      default:
         unreachable("Unhandled semaphore handle type");
   }

   if (ret) {
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
   dst->syncobj = syncobj;
   dst->kind = TU_SEMAPHORE_SYNCOBJ;

   return VK_SUCCESS;
}

VkResult
tu_GetSemaphoreFdKHR(VkDevice _device,
                     const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_semaphore, sem, pGetFdInfo->semaphore);
   int ret;
   uint32_t syncobj_handle;

   if (sem->temporary.kind != TU_SEMAPHORE_NONE) {
      assert(sem->temporary.kind == TU_SEMAPHORE_SYNCOBJ);
      syncobj_handle = sem->temporary.syncobj;
   } else {
      assert(sem->permanent.kind == TU_SEMAPHORE_SYNCOBJ);
      syncobj_handle = sem->permanent.syncobj;
   }

   switch(pGetFdInfo->handleType) {
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
      ret = drmSyncobjHandleToFD(device->physical_device->local_fd, syncobj_handle, pFd);
      break;
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT:
      ret = drmSyncobjExportSyncFile(device->physical_device->local_fd, syncobj_handle, pFd);
      if (!ret) {
         if (sem->temporary.kind != TU_SEMAPHORE_NONE) {
            tu_semaphore_part_destroy(device, &sem->temporary);
         } else {
            drmSyncobjReset(device->physical_device->local_fd, &syncobj_handle, 1);
         }
      }
      break;
   default:
      unreachable("Unhandled semaphore handle type");
   }

   if (ret)
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   return VK_SUCCESS;
}

static bool tu_has_syncobj(struct tu_physical_device *pdev)
{
   uint64_t value;
   if (drmGetCap(pdev->local_fd, DRM_CAP_SYNCOBJ, &value))
      return false;
   return value && pdev->msm_major_version == 1 && pdev->msm_minor_version >= 6;
}

void
tu_GetPhysicalDeviceExternalSemaphoreProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
   VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
   TU_FROM_HANDLE(tu_physical_device, pdev, physicalDevice);

   if (tu_has_syncobj(pdev) &&
       (pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT ||
        pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT)) {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->compatibleHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
      pExternalSemaphoreProperties->compatibleHandleTypes = 0;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
   }
}

VkResult
tu_QueueSubmit(VkQueue _queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence _fence)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   VkResult result;

   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo *submit = pSubmits + i;
      const bool last_submit = (i == submitCount - 1);
      struct drm_msm_gem_submit_syncobj *in_syncobjs = NULL, *out_syncobjs = NULL;
      uint32_t nr_in_syncobjs, nr_out_syncobjs;
      struct tu_bo_list bo_list;
      tu_bo_list_init(&bo_list);

      result = tu_get_semaphore_syncobjs(pSubmits[i].pWaitSemaphores,
                                         pSubmits[i].waitSemaphoreCount,
                                         false, &in_syncobjs, &nr_in_syncobjs);
      if (result != VK_SUCCESS) {
         return tu_device_set_lost(queue->device,
                                   "failed to allocate space for semaphore submission\n");
      }

      result = tu_get_semaphore_syncobjs(pSubmits[i].pSignalSemaphores,
                                         pSubmits[i].signalSemaphoreCount,
                                         false, &out_syncobjs, &nr_out_syncobjs);
      if (result != VK_SUCCESS) {
         free(in_syncobjs);
         return tu_device_set_lost(queue->device,
                                   "failed to allocate space for semaphore submission\n");
      }

      uint32_t entry_count = 0;
      for (uint32_t j = 0; j < submit->commandBufferCount; ++j) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBuffers[j]);
         entry_count += cmdbuf->cs.entry_count;
      }

      struct drm_msm_gem_submit_cmd cmds[entry_count];
      uint32_t entry_idx = 0;
      for (uint32_t j = 0; j < submit->commandBufferCount; ++j) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBuffers[j]);
         struct tu_cs *cs = &cmdbuf->cs;
         for (unsigned i = 0; i < cs->entry_count; ++i, ++entry_idx) {
            cmds[entry_idx].type = MSM_SUBMIT_CMD_BUF;
            cmds[entry_idx].submit_idx =
               tu_bo_list_add(&bo_list, cs->entries[i].bo,
                              MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
            cmds[entry_idx].submit_offset = cs->entries[i].offset;
            cmds[entry_idx].size = cs->entries[i].size;
            cmds[entry_idx].pad = 0;
            cmds[entry_idx].nr_relocs = 0;
            cmds[entry_idx].relocs = 0;
         }

         tu_bo_list_merge(&bo_list, &cmdbuf->bo_list);
      }

      uint32_t flags = MSM_PIPE_3D0;
      if (nr_in_syncobjs) {
         flags |= MSM_SUBMIT_SYNCOBJ_IN;
      }
      if (nr_out_syncobjs) {
         flags |= MSM_SUBMIT_SYNCOBJ_OUT;
      }

      if (last_submit) {
         flags |= MSM_SUBMIT_FENCE_FD_OUT;
      }

      struct drm_msm_gem_submit req = {
         .flags = flags,
         .queueid = queue->msm_queue_id,
         .bos = (uint64_t)(uintptr_t) bo_list.bo_infos,
         .nr_bos = bo_list.count,
         .cmds = (uint64_t)(uintptr_t)cmds,
         .nr_cmds = entry_count,
         .in_syncobjs = (uint64_t)(uintptr_t)in_syncobjs,
         .out_syncobjs = (uint64_t)(uintptr_t)out_syncobjs,
         .nr_in_syncobjs = nr_in_syncobjs,
         .nr_out_syncobjs = nr_out_syncobjs,
         .syncobj_stride = sizeof(struct drm_msm_gem_submit_syncobj),
      };

      int ret = drmCommandWriteRead(queue->device->physical_device->local_fd,
                                    DRM_MSM_GEM_SUBMIT,
                                    &req, sizeof(req));
      if (ret) {
         free(in_syncobjs);
         free(out_syncobjs);
         return tu_device_set_lost(queue->device, "submit failed: %s\n",
                                   strerror(errno));
      }

      tu_bo_list_destroy(&bo_list);
      free(in_syncobjs);
      free(out_syncobjs);

      tu_semaphores_remove_temp(queue->device, pSubmits[i].pWaitSemaphores,
                                pSubmits[i].waitSemaphoreCount);
      if (last_submit) {
         /* no need to merge fences as queue execution is serialized */
         tu_fence_update_fd(&queue->submit_fence, req.fence_fd);
      } else if (last_submit) {
         close(req.fence_fd);
      }
   }

   if (_fence != VK_NULL_HANDLE) {
      TU_FROM_HANDLE(tu_fence, fence, _fence);
      tu_fence_copy(fence, &queue->submit_fence);
   }

   return VK_SUCCESS;
}
