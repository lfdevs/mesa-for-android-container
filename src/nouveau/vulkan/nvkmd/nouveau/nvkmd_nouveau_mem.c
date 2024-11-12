/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvkmd_nouveau.h"

#include "nouveau_bo.h"
#include "vk_log.h"

#include <sys/mman.h>

VkResult
nvkmd_nouveau_alloc_mem(struct nvkmd_dev *dev,
                        struct vk_object_base *log_obj,
                        uint64_t size_B, uint64_t align_B,
                        enum nvkmd_mem_flags flags,
                        struct nvkmd_mem **mem_out)
{
   return nvkmd_nouveau_alloc_tiled_mem(dev, log_obj, size_B, align_B,
                                        0 /* pte_kind */, 0 /* tile_mode */,
                                        flags, mem_out);
}

static VkResult
create_mem_or_close_bo(struct nvkmd_nouveau_dev *dev,
                       struct vk_object_base *log_obj,
                       enum nvkmd_mem_flags mem_flags,
                       struct nouveau_ws_bo *bo,
                       enum nvkmd_va_flags va_flags,
                       uint8_t pte_kind, uint64_t va_align_B,
                       struct nvkmd_mem **mem_out)
{
   const uint64_t size_B = bo->size;
   VkResult result;

   struct nvkmd_nouveau_mem *mem = CALLOC_STRUCT(nvkmd_nouveau_mem);
   if (mem == NULL) {
      result = vk_error(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_bo;
   }

   nvkmd_mem_init(&dev->base, &mem->base, &nvkmd_nouveau_mem_ops,
                  mem_flags, size_B, dev->base.pdev->bind_align_B);
   mem->bo = bo;

   result = nvkmd_dev_alloc_va(&dev->base, log_obj,
                               va_flags, pte_kind,
                               size_B, va_align_B,
                               0 /* fixed_addr */,
                               &mem->base.va);
   if (result != VK_SUCCESS)
      goto fail_mem;

   result = nvkmd_va_bind_mem(mem->base.va, log_obj, 0 /* va_offset_B */,
                              &mem->base, 0 /* mem_offset_B */, size_B);
   if (result != VK_SUCCESS)
      goto fail_va;

   *mem_out = &mem->base;

   return VK_SUCCESS;

fail_va:
   nvkmd_va_free(mem->base.va);
fail_mem:
   FREE(mem);
fail_bo:
   nouveau_ws_bo_destroy(bo);

   return result;
}

VkResult
nvkmd_nouveau_alloc_tiled_mem(struct nvkmd_dev *_dev,
                              struct vk_object_base *log_obj,
                              uint64_t size_B, uint64_t align_B,
                              uint8_t pte_kind, uint16_t tile_mode,
                              enum nvkmd_mem_flags flags,
                              struct nvkmd_mem **mem_out)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);
   const struct nv_device_info *dev_info = &dev->base.pdev->dev_info;

   /* Only one placement flag may be specified */
   assert(util_bitcount(flags & (NVKMD_MEM_LOCAL |
                                 NVKMD_MEM_GART |
                                 NVKMD_MEM_VRAM)) == 1);
   enum nouveau_ws_bo_flags domains = 0;
   if (flags & NVKMD_MEM_LOCAL) {
      domains |= NOUVEAU_WS_BO_GART;
      if (dev_info->vram_size_B > 0)
         domains |= NOUVEAU_WS_BO_VRAM;
   } else if (flags & NVKMD_MEM_GART) {
      domains |= NOUVEAU_WS_BO_GART;
   } else if (flags & NVKMD_MEM_VRAM) {
      domains |= NOUVEAU_WS_BO_VRAM;
   }

   if (dev->base.pdev->debug_flags & NVK_DEBUG_FORCE_GART)
      domains = NOUVEAU_WS_BO_GART;

   /* TODO:
    *
    * VRAM maps on Kepler appear to be broken and we don't really know why.
    * My NVIDIA contact doesn't remember them not working so they probably
    * should but they don't today.  Force everything that may be mapped to
    * use GART for now.
    */
   if (dev_info->chipset < 0x110 && (flags & NOUVEAU_WS_BO_MAP)) {
      assert(domains & NOUVEAU_WS_BO_GART);
      domains = NOUVEAU_WS_BO_GART;
   }

   const uint32_t mem_align_B = _dev->pdev->bind_align_B;
   size_B = align64(size_B, mem_align_B);

   assert(util_is_power_of_two_or_zero64(align_B));
   const uint64_t va_align_B = MAX2(mem_align_B, align_B);

   enum nouveau_ws_bo_flags nouveau_flags = domains;
   if (flags & NVKMD_MEM_CAN_MAP)
      nouveau_flags |= NOUVEAU_WS_BO_MAP;
   if (!(flags & NVKMD_MEM_SHARED))
      nouveau_flags |= NOUVEAU_WS_BO_NO_SHARE;

   struct nouveau_ws_bo *bo = nouveau_ws_bo_new_tiled(dev->ws_dev,
                                                      size_B, mem_align_B,
                                                      pte_kind, tile_mode,
                                                      nouveau_flags);
   if (bo == NULL)
      return vk_errorf(log_obj, VK_ERROR_OUT_OF_DEVICE_MEMORY, "%m");

   enum nvkmd_va_flags va_flags = 0;
   if (domains == NOUVEAU_WS_BO_GART)
      va_flags |= NVKMD_VA_GART;

   return create_mem_or_close_bo(dev, log_obj, flags, bo,
                                 va_flags, pte_kind, va_align_B,
                                 mem_out);
}

VkResult
nvkmd_nouveau_import_dma_buf(struct nvkmd_dev *_dev,
                             struct vk_object_base *log_obj,
                             int fd, struct nvkmd_mem **mem_out)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);

   struct nouveau_ws_bo *bo = nouveau_ws_bo_from_dma_buf(dev->ws_dev, fd);
   if (bo == NULL)
      return vk_errorf(log_obj, VK_ERROR_INVALID_EXTERNAL_HANDLE, "%m");

   enum nvkmd_mem_flags flags = NVKMD_MEM_SHARED;

   /* We always set LOCAL for shared things because we don't know where the
    * kernel will place it.  The query only tells us where it is.
    */
   flags |= NVKMD_MEM_LOCAL;

   if (bo->flags & NOUVEAU_WS_BO_MAP)
      flags |= NVKMD_MEM_CAN_MAP;

   return create_mem_or_close_bo(dev, log_obj, flags, bo,
                                 0 /* va_flags */,
                                 0 /* pte_kind */,
                                 0 /* va_align_B */,
                                 mem_out);
}

static void
nvkmd_nouveau_mem_free(struct nvkmd_mem *_mem)
{
   struct nvkmd_nouveau_mem *mem = nvkmd_nouveau_mem(_mem);

   nvkmd_va_free(mem->base.va);
   nouveau_ws_bo_destroy(mem->bo);
   FREE(mem);
}

static VkResult
nvkmd_nouveau_mem_map(struct nvkmd_mem *_mem,
                      struct vk_object_base *log_obj,
                      enum nvkmd_mem_map_flags map_flags,
                      void *fixed_addr,
                      void **map_out)
{
   struct nvkmd_nouveau_mem *mem = nvkmd_nouveau_mem(_mem);
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_mem->dev);

   int prot = 0;
   if (map_flags & NVKMD_MEM_MAP_RD)
      prot |= PROT_READ;
   if (map_flags & NVKMD_MEM_MAP_WR)
      prot |= PROT_WRITE;

   int flags = MAP_SHARED;
   if (map_flags & NVKMD_MEM_MAP_FIXED)
      flags |= MAP_FIXED;

   void *map = mmap(fixed_addr, mem->base.size_B, prot, flags,
                    dev->ws_dev->fd, mem->bo->map_handle);
   if (map == MAP_FAILED)
      return vk_error(log_obj, VK_ERROR_MEMORY_MAP_FAILED);

   *map_out = map;

   return VK_SUCCESS;
}

static void
nvkmd_nouveau_mem_unmap(struct nvkmd_mem *_mem,
                        enum nvkmd_mem_map_flags flags,
                        void *map)
{
   struct nvkmd_nouveau_mem *mem = nvkmd_nouveau_mem(_mem);

   munmap(map, mem->base.size_B);
}

static VkResult
nvkmd_nouveau_mem_overmap(struct nvkmd_mem *_mem,
                          struct vk_object_base *log_obj,
                          enum nvkmd_mem_map_flags flags,
                          void *map)
{
   struct nvkmd_nouveau_mem *mem = nvkmd_nouveau_mem(_mem);

   void *new_map = mmap(map, mem->base.size_B, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
   if (new_map == MAP_FAILED) {
      return vk_errorf(log_obj, VK_ERROR_MEMORY_MAP_FAILED,
                       "Failed to map over original mapping");
   }

   assert(new_map == map);

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_mem_export_dma_buf(struct nvkmd_mem *_mem,
                                 struct vk_object_base *log_obj,
                                 int *fd_out)
{
   struct nvkmd_nouveau_mem *mem = nvkmd_nouveau_mem(_mem);

   int err = nouveau_ws_bo_dma_buf(mem->bo, fd_out);
   if (err)
      return vk_errorf(log_obj, VK_ERROR_TOO_MANY_OBJECTS,
                       "Failed to export dma-buf: %m");

   return VK_SUCCESS;
}

static uint32_t
nvkmd_nouveau_mem_log_handle(struct nvkmd_mem *_mem)
{
   return nvkmd_nouveau_mem(_mem)->bo->handle;
}

const struct nvkmd_mem_ops nvkmd_nouveau_mem_ops = {
   .free = nvkmd_nouveau_mem_free,
   .map = nvkmd_nouveau_mem_map,
   .unmap = nvkmd_nouveau_mem_unmap,
   .overmap = nvkmd_nouveau_mem_overmap,
   .export_dma_buf = nvkmd_nouveau_mem_export_dma_buf,
   .log_handle = nvkmd_nouveau_mem_log_handle,
};
