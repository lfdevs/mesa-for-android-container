/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvkmd.h"
#include "nouveau/nvkmd_nouveau.h"

#include <inttypes.h>

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
   result = mem->ops->map(mem, log_obj, map_flags, NULL);
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

   if (mem->map != NULL)
      mem->ops->unmap(mem);

   mem->ops->free(mem);
}
