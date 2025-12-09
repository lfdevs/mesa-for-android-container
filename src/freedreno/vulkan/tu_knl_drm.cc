/*
 * Copyright © 2018 Google, Inc.
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "tu_knl_drm.h"
#include "tu_device.h"
#include "tu_queue.h"
#include "tu_rmv.h"

VkResult
tu_allocate_userspace_iova(struct tu_device *dev,
                           uint64_t size,
                           uint64_t client_iova,
                           enum tu_bo_alloc_flags flags,
                           uint64_t *iova)
{
   *iova = 0;

   if (flags & TU_BO_ALLOC_REPLAYABLE) {
      if (client_iova) {
         if (util_vma_heap_alloc_addr(&dev->vma, client_iova, size)) {
            *iova = client_iova;
         } else {
            return VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
         }
      } else {
         /* We have to separate replayable IOVAs from ordinary one in order to
          * for them not to clash. The easiest way to do this is to allocate
          * them from the other end of the address space.
          */
         dev->vma.alloc_high = true;
         *iova = util_vma_heap_alloc(&dev->vma, size, os_page_size);
      }
   } else {
      dev->vma.alloc_high = false;
      *iova = util_vma_heap_alloc(&dev->vma, size, os_page_size);
   }

   if (!*iova)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   return VK_SUCCESS;
}

int
tu_drm_export_dmabuf(struct tu_device *dev, struct tu_bo *bo)
{
   int prime_fd;
   int ret = drmPrimeHandleToFD(dev->fd, bo->gem_handle,
                                DRM_CLOEXEC | DRM_RDWR, &prime_fd);

   return ret == 0 ? prime_fd : -1;
}

void
tu_bo_list_del(struct tu_device *dev, struct tu_bo *bo)
{
   mtx_lock(&dev->bo_mutex);
   dev->submit_bo_count--;
   dev->submit_bo_list[bo->submit_bo_list_idx] = dev->submit_bo_list[dev->submit_bo_count];

   struct tu_bo* exchanging_bo = tu_device_lookup_bo(dev, dev->submit_bo_list[bo->submit_bo_list_idx].handle);
   exchanging_bo->submit_bo_list_idx = bo->submit_bo_list_idx;

   if (bo->implicit_sync)
      dev->implicit_sync_bo_count--;

   mtx_unlock(&dev->bo_mutex);
}

void
tu_bo_make_zombie(struct tu_device *dev, struct tu_bo *bo)
{
   mtx_lock(&dev->vma_mutex);
   struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
         u_vector_add(&dev->zombie_vmas);
   vma->gem_handle = bo->gem_handle;
#ifdef TU_HAS_VIRTIO
   vma->res_id = bo->res_id;
#endif
   vma->iova = bo->iova;
   vma->size = bo->size;
   vma->fence = p_atomic_read(&dev->queues[0]->fence);

   /* Must be cleared under the VMA mutex, or another thread could race to
    * reap the VMA, closing the BO and letting a new GEM allocation produce
    * this handle again.
    */
   memset(bo, 0, sizeof(*bo));
   mtx_unlock(&dev->vma_mutex);
}

void *
msm_submit_create(struct tu_device *device)
{
   return vk_zalloc(&device->vk.alloc, sizeof(struct tu_msm_queue_submit), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
}

void
msm_submit_finish(struct tu_device *device,
                  void *_submit)
{
   struct tu_msm_queue_submit *submit =
      (struct tu_msm_queue_submit *)_submit;

   util_dynarray_fini(&submit->commands);
   util_dynarray_fini(&submit->command_bos);
   util_dynarray_fini(&submit->binds);
   vk_free(&device->vk.alloc, submit);
}

void
msm_submit_add_entries(struct tu_device *device, void *_submit,
                       struct tu_cs_entry *entries, unsigned num_entries)
{
   struct tu_msm_queue_submit *submit =
      (struct tu_msm_queue_submit *)_submit;

   bool has_vm_bind = device->physical_device->has_vm_bind;

   struct drm_msm_gem_submit_cmd *cmds = (struct drm_msm_gem_submit_cmd *)
      util_dynarray_grow(&submit->commands, struct drm_msm_gem_submit_cmd,
                         num_entries);

   const struct tu_bo **bos = (const struct tu_bo **)
      util_dynarray_grow(&submit->command_bos, struct tu_bo *,
                         num_entries);

   for (unsigned i = 0; i < num_entries; i++) {
      cmds[i].type = MSM_SUBMIT_CMD_BUF;
      cmds[i].submit_idx = has_vm_bind ? 0 : entries[i].bo->submit_bo_list_idx;
      cmds[i].submit_offset = has_vm_bind ? 0 : entries[i].offset;
      cmds[i].size = entries[i].size;
      cmds[i].pad = 0;
      cmds[i].nr_relocs = 0;
      if (has_vm_bind)
         cmds[i].iova = entries[i].bo->iova + entries[i].offset;
      else
         cmds[i].relocs = 0;
      bos[i] = entries[i].bo;
   }
}

void
msm_submit_add_bind(struct tu_device *device,
                    void *_submit,
                    struct tu_sparse_vma *vma, uint64_t vma_offset,
                    struct tu_bo *bo, uint64_t bo_offset,
                    uint64_t size)
{
   struct tu_msm_queue_submit *submit =
      (struct tu_msm_queue_submit *)_submit;

   struct drm_msm_vm_bind_op bind = {
      .op = bo ? MSM_VM_BIND_OP_MAP :
         ((vma->flags & TU_SPARSE_VMA_MAP_ZERO) ?
            MSM_VM_BIND_OP_MAP_NULL : MSM_VM_BIND_OP_UNMAP),
      .handle = bo ? bo->gem_handle : 0,
      .obj_offset = bo_offset,
      .iova = vma->msm.iova + vma_offset,
      .range = size,
   };

   util_dynarray_append(&submit->binds, bind);
}

