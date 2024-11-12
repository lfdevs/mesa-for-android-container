/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvkmd.h"
#include "nouveau/nvkmd_nouveau.h"

#include <inttypes.h>

void
nvkmd_dev_track_mem(struct nvkmd_dev *dev,
                    struct nvkmd_mem *mem)
{
   if (mem->link.next == NULL) {
      simple_mtx_lock(&dev->mems_mutex);
      list_addtail(&mem->link, &dev->mems);
      simple_mtx_unlock(&dev->mems_mutex);
   }
}

static void
nvkmd_dev_untrack_mem(struct nvkmd_dev *dev,
                      struct nvkmd_mem *mem)
{
   if (mem->link.next != NULL) {
      simple_mtx_lock(&dev->mems_mutex);
      list_del(&mem->link);
      simple_mtx_unlock(&dev->mems_mutex);
   }
}

static struct nvkmd_mem *
nvkmd_dev_lookup_mem_by_va_locked(struct nvkmd_dev *dev,
                                  uint64_t addr,
                                  uint64_t *offset_out)
{
   list_for_each_entry(struct nvkmd_mem, mem, &dev->mems, link) {
      if (mem->va == NULL || addr < mem->va->addr)
         continue;

      const uint64_t offset = addr - mem->va->addr;
      if (offset < mem->va->size_B) {
         if (offset_out != NULL)
            *offset_out = offset;
         return nvkmd_mem_ref(mem);
      }
   }

   return NULL;
}

struct nvkmd_mem *
nvkmd_dev_lookup_mem_by_va(struct nvkmd_dev *dev,
                           uint64_t addr,
                           uint64_t *offset_out)
{
   simple_mtx_lock(&dev->mems_mutex);
   struct nvkmd_mem *mem =
      nvkmd_dev_lookup_mem_by_va_locked(dev, addr, offset_out);
   simple_mtx_unlock(&dev->mems_mutex);
   return mem;
}

void
nvkmd_mem_init(struct nvkmd_dev *dev,
               struct nvkmd_mem *mem,
               const struct nvkmd_mem_ops *ops,
               enum nvkmd_mem_flags flags,
               uint64_t size_B,
               uint32_t bind_align_B)
{
   *mem = (struct nvkmd_mem) {
      .ops = ops,
      .dev = dev,
      .refcnt = 1,
      .flags = flags,
      .bind_align_B = bind_align_B,
      .size_B = size_B,
   };

   simple_mtx_init(&mem->map_mutex, mtx_plain);
}

VkResult
nvkmd_try_create_pdev_for_drm(struct _drmDevice *drm_device,
                              struct vk_object_base *log_obj,
                              enum nvk_debug debug_flags,
                              struct nvkmd_pdev **pdev_out)
{
   return nvkmd_nouveau_try_create_pdev(drm_device, log_obj,
                                        debug_flags, pdev_out);
}

VkResult
nvkmd_dev_alloc_mapped_mem(struct nvkmd_dev *dev,
                           struct vk_object_base *log_obj,
                           uint64_t size_B, uint64_t align_B,
                           enum nvkmd_mem_flags flags,
                           enum nvkmd_mem_map_flags map_flags,
                           struct nvkmd_mem **mem_out)
{
   struct nvkmd_mem *mem;
   VkResult result;

   result = nvkmd_dev_alloc_mem(dev, log_obj, size_B, align_B,
                                flags | NVKMD_MEM_CAN_MAP, &mem);
   if (result != VK_SUCCESS)
      return result;

   assert(!(map_flags & NVKMD_MEM_MAP_FIXED));
   result = nvkmd_mem_map(mem, log_obj, map_flags, NULL, NULL);
   if (result != VK_SUCCESS) {
      mem->ops->free(mem);
      return result;
   }

   *mem_out = mem;

   return VK_SUCCESS;
}

VkResult MUST_CHECK
nvkmd_dev_alloc_va(struct nvkmd_dev *dev,
                   struct vk_object_base *log_obj,
                   enum nvkmd_va_flags flags, uint8_t pte_kind,
                   uint64_t size_B, uint64_t align_B,
                   uint64_t fixed_addr, struct nvkmd_va **va_out)
{
   VkResult result = dev->ops->alloc_va(dev, log_obj, flags, pte_kind,
                                        size_B, align_B, fixed_addr, va_out);
   if (result != VK_SUCCESS)
      return result;

   if (unlikely(dev->pdev->debug_flags & NVK_DEBUG_VM)) {
      const char *sparse = (flags & NVKMD_VA_SPARSE) ? " sparse" : "";
      fprintf(stderr, "alloc va [0x%" PRIx64 ", 0x%" PRIx64 ")%s\n",
              (*va_out)->addr, (*va_out)->addr + size_B, sparse);
   }

   return VK_SUCCESS;
}

void
nvkmd_va_free(struct nvkmd_va *va)
{
   if (unlikely(va->dev->pdev->debug_flags & NVK_DEBUG_VM)) {
      const char *sparse = (va->flags & NVKMD_VA_SPARSE) ? " sparse" : "";
      fprintf(stderr, "free va [0x%" PRIx64 ", 0x%" PRIx64 ")%s\n",
              va->addr, va->addr + va->size_B, sparse);
   }

   va->ops->free(va);
}

static inline void
log_va_bind_mem(struct nvkmd_va *va,
                uint64_t va_offset_B,
                struct nvkmd_mem *mem,
                uint64_t mem_offset_B,
                uint64_t range_B)
{
   fprintf(stderr, "bind vma mem<0x%" PRIx32 ">"
                   "[0x%" PRIx64 ", 0x%" PRIx64 ") to "
                   "[0x%" PRIx64 ", 0x%" PRIx64 ")\n",
           mem->ops->log_handle(mem),
           mem_offset_B, mem_offset_B + range_B,
           va->addr, va->addr + range_B);
}

static inline void
log_va_unbind(struct nvkmd_va *va,
              uint64_t va_offset_B,
              uint64_t range_B)
{
   fprintf(stderr, "unbind vma [0x%" PRIx64 ", 0x%" PRIx64 ")\n",
           va->addr, va->addr + range_B);
}

VkResult MUST_CHECK
nvkmd_va_bind_mem(struct nvkmd_va *va,
                  struct vk_object_base *log_obj,
                  uint64_t va_offset_B,
                  struct nvkmd_mem *mem,
                  uint64_t mem_offset_B,
                  uint64_t range_B)
{
   assert(va_offset_B <= va->size_B);
   assert(va_offset_B + range_B <= va->size_B);
   assert(mem_offset_B <= mem->size_B);
   assert(mem_offset_B + range_B <= mem->size_B);

   assert(va->addr % mem->bind_align_B == 0);
   assert(va_offset_B % mem->bind_align_B == 0);
   assert(mem_offset_B % mem->bind_align_B == 0);
   assert(range_B % mem->bind_align_B == 0);

   if (unlikely(va->dev->pdev->debug_flags & NVK_DEBUG_VM))
      log_va_bind_mem(va, va_offset_B, mem, mem_offset_B, range_B);

   return va->ops->bind_mem(va, log_obj, va_offset_B,
                            mem, mem_offset_B, range_B);
}

VkResult MUST_CHECK
nvkmd_va_unbind(struct nvkmd_va *va,
                struct vk_object_base *log_obj,
                uint64_t va_offset_B,
                uint64_t range_B)
{
   assert(va_offset_B <= va->size_B);
   assert(va_offset_B + range_B <= va->size_B);

   if (unlikely(va->dev->pdev->debug_flags & NVK_DEBUG_VM))
      log_va_unbind(va, va_offset_B, range_B);

   return va->ops->unbind(va, log_obj, va_offset_B, range_B);
}

VkResult MUST_CHECK
nvkmd_ctx_bind(struct nvkmd_ctx *ctx,
               struct vk_object_base *log_obj,
               uint32_t bind_count,
               const struct nvkmd_ctx_bind *binds)
{
   for (uint32_t i = 0; i < bind_count; i++) {
      assert(binds[i].va_offset_B <= binds[i].va->size_B);
      assert(binds[i].va_offset_B + binds[i].range_B <= binds[i].va->size_B);
      if (binds[i].op == NVKMD_BIND_OP_BIND) {
         assert(binds[i].mem_offset_B % binds[i].mem->bind_align_B == 0);
         assert(binds[i].mem_offset_B <= binds[i].mem->size_B);
         assert(binds[i].mem_offset_B + binds[i].range_B <=
                binds[i].mem->size_B);

         assert(binds[i].va->addr % binds[i].mem->bind_align_B == 0);
         assert(binds[i].va_offset_B % binds[i].mem->bind_align_B == 0);
         assert(binds[i].mem_offset_B % binds[i].mem->bind_align_B == 0);
         assert(binds[i].range_B % binds[i].mem->bind_align_B == 0);
      } else {
         assert(binds[i].mem == NULL);
      }
   }

   if (unlikely(ctx->dev->pdev->debug_flags & NVK_DEBUG_VM)) {
      for (uint32_t i = 0; i < bind_count; i++) {
         if (binds[i].op == NVKMD_BIND_OP_BIND) {
            log_va_bind_mem(binds[i].va, binds[i].va_offset_B,
                            binds[i].mem, binds[i].mem_offset_B,
                            binds[i].range_B);
         } else {
            log_va_unbind(binds[i].va, binds[i].va_offset_B, binds[i].range_B);
         }
      }
   }

   return ctx->ops->bind(ctx, log_obj, bind_count, binds);
}

void
nvkmd_mem_unref(struct nvkmd_mem *mem)
{
   assert(p_atomic_read(&mem->refcnt) > 0);
   if (!p_atomic_dec_zero(&mem->refcnt))
      return;

   if (mem->client_map != NULL)
      mem->ops->unmap(mem, NVKMD_MEM_MAP_CLIENT, mem->client_map);

   if (mem->map != NULL)
      mem->ops->unmap(mem, 0, mem->map);

   nvkmd_dev_untrack_mem(mem->dev, mem);

   mem->ops->free(mem);
}

VkResult
nvkmd_mem_map(struct nvkmd_mem *mem, struct vk_object_base *log_obj,
              enum nvkmd_mem_map_flags flags, void *fixed_addr,
              void **map_out)
{
   void *map = NULL;

   assert((fixed_addr == NULL) == !(flags & NVKMD_MEM_MAP_FIXED));

   if (flags & NVKMD_MEM_MAP_CLIENT) {
      assert(mem->client_map == NULL);

      VkResult result = mem->ops->map(mem, log_obj, flags, fixed_addr, &map);
      if (result != VK_SUCCESS)
         return result;

      mem->client_map = map;
   } else {
      assert(!(flags & NVKMD_MEM_MAP_FIXED));

      simple_mtx_lock(&mem->map_mutex);

      assert((mem->map_cnt == 0) == (mem->map == NULL));
      mem->map_cnt++;

      VkResult result = VK_SUCCESS;
      if (mem->map == NULL) {
         /* We always map read/write for internal maps since they're reference
          * counted and otherwise we don't have a good way to add permissions
          * after the fact.
          */
         result = mem->ops->map(mem, log_obj, NVKMD_MEM_MAP_RDWR, NULL, &map);
         if (result == VK_SUCCESS)
            mem->map = map;
      } else {
         map = mem->map;
      }

      simple_mtx_unlock(&mem->map_mutex);

      if (result != VK_SUCCESS)
         return result;
   }

   if (map_out != NULL)
      *map_out = map;

   return VK_SUCCESS;
}

void
nvkmd_mem_unmap(struct nvkmd_mem *mem, enum nvkmd_mem_map_flags flags)
{
   if (flags & NVKMD_MEM_MAP_CLIENT) {
      assert(mem->client_map != NULL);
      mem->ops->unmap(mem, flags, mem->client_map);
      mem->client_map = NULL;
   } else {
      assert(mem->map != NULL);
      simple_mtx_lock(&mem->map_mutex);
      if (--mem->map_cnt == 0) {
         mem->ops->unmap(mem, flags, mem->map);
         mem->map = NULL;
      }
      simple_mtx_unlock(&mem->map_mutex);
   }
}
