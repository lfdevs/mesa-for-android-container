/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DEVICE_H
#define PANVK_DEVICE_H

#include <stdint.h>

#include "vk_debug_utils.h"
#include "vk_device.h"
#include "vk_meta.h"

#include "panvk_blend.h"
#include "panvk_instance.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_physical_device.h"
#include "panvk_utrace_perfetto.h"

#include "kmod/pan_kmod.h"
#include "util/perf/u_trace.h"

#include "util/simple_mtx.h"
#include "util/u_call_once.h"
#include "util/u_printf.h"
#include "util/vma.h"

/* On JM hardware, we need to allocate a buffer depending on vertex count.
 *
 * As a result, for indirect and indexed draw we allocate a large buffer with
 * alloc on fault set.
 *
 * The size of that buffer is calculated assuming a max of 2 millions vertices
 * and 18 attributes per vertex (16 user attributes, 2 specials)
 */

#define PANVK_JM_MAX_VERTICES_INDIRECT                (2000000)
#define PANVK_JM_MAX_PER_VTX_ATTRIBUTES_INDIRECT_SIZE (18 * 4)

struct panvk_precomp_cache;
struct panvk_device_draw_context;

enum panvk_queue_family {
   PANVK_QUEUE_FAMILY_GPU,
   PANVK_QUEUE_FAMILY_BIND,
   PANVK_QUEUE_FAMILY_COUNT,
};

struct panvk_device {
   struct vk_device vk;

   struct {
      simple_mtx_t lock;
      struct util_vma_heap heap;
      struct util_vma_heap fixed_heap;
      struct util_vma_heap *priv_heap;
      bool split_heap;
      bool extended_range;
   } as;

   struct {
      struct pan_kmod_vm *vm;
      struct pan_kmod_dev *dev;
      struct pan_kmod_allocator allocator;
   } kmod;

   struct panvk_priv_bo *tiler_heap;
   struct panvk_priv_bo *indirect_varying_buffer;
   struct panvk_priv_bo *sample_positions;

   struct {
      struct panvk_priv_bo *handlers_bo;
      uint32_t handler_stride;
   } tiler_oom;

   struct vk_meta_device meta;

   struct {
      struct panvk_pool rw;
      struct panvk_pool rw_nc;
      struct panvk_pool exec;
   } mempools;

   struct {
      util_once_flag blackhole_once;
      struct pan_kmod_bo *blackhole;
   } sparse_mem;

   /* For each subqueue, maximum size of the register dump region needed by
    * exception handlers or functions */
   uint32_t *dump_region_size;

   struct vk_device_dispatch_table cmd_dispatch;

   struct panvk_precomp_cache *precomp_cache;

   struct {
      struct u_trace_context utctx;
#ifdef HAVE_PERFETTO
      struct panvk_utrace_perfetto utp;
#endif
      /* Timestamp + indirect data storage */
      struct util_vma_heap copy_buf_heap;
      struct panvk_priv_bo *copy_buf_heap_bo;
      simple_mtx_t copy_buf_heap_lock;
   } utrace;

   struct panvk_device_draw_context* draw_ctx;

   struct {
      struct pandecode_context *decode_ctx;
   } debug;

   struct {
      struct u_printf_ctx ctx;
      struct panvk_priv_bo *bo;
   } printf;

   union {
      struct {
         struct {
            uint8_t count;
            uint8_t iter_count;
            uint16_t all_mask;
            uint16_t all_iters_mask;
         } sb;
      } csf;
   };

   int drm_fd;
};

VK_DEFINE_HANDLE_CASTS(panvk_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)

static inline struct panvk_device *
to_panvk_device(struct vk_device *dev)
{
   return container_of(dev, struct panvk_device, vk);
}

static inline void
panvk_address_binding_report(struct panvk_device *dev,
                             struct vk_object_base *object, uint64_t base,
                             uint64_t size, VkDeviceAddressBindingTypeEXT type)
{
   vk_address_binding_report(dev->vk.physical->instance,
                             object ? object : &dev->vk.base, base, size, type);
}

static inline uint32_t
panvk_device_adjust_bo_flags(const struct panvk_device *device,
                             uint32_t bo_flags)
{
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(device->vk.physical);

   /* Dumping of GPU buffers messes up with CPU caches by loading buffers
    * into cachelines which are sometimes assumed to be invalidated by the
    * rest of the code. We could explicitly invalidate after a dump, but
    * this is simpler to just disable cached-mappings when PANVK_DEBUG="dump".
    * Tracing has the same fundamental issue, but we'll take care of that
    * at the desc/CS pools level.
    */
   const bool allow_wb_mmap =
      (pdev->kmod.dev->props.supported_bo_flags & PAN_KMOD_BO_FLAG_WB_MMAP) &&
      !PANVK_DEBUG(DUMP) && !PANVK_DEBUG(NO_WB_MMAP);

   if (!allow_wb_mmap)
      bo_flags &= ~PAN_KMOD_BO_FLAG_WB_MMAP;

   if (PANVK_DEBUG(DUMP) || PANVK_DEBUG(TRACE))
      bo_flags &= ~PAN_KMOD_BO_FLAG_NO_MMAP;

   if (!(device->kmod.dev->props.supported_bo_flags &
         PAN_KMOD_BO_FLAG_GPU_UNCACHED))
      bo_flags &= ~PAN_KMOD_BO_FLAG_GPU_UNCACHED;

   return bo_flags;
}

static inline uint64_t
panvk_get_gpu_page_size(const struct panvk_device *device)
{
   return (uint64_t)1 << (ffsll(device->kmod.vm->pgsize_bitmap) - 1);
}

static inline uint64_t
panvk_as_alloc(struct panvk_device *device, struct util_vma_heap *heap,
               uint64_t size, uint64_t alignment)
{
   simple_mtx_lock(&device->as.lock);
   uint64_t address = util_vma_heap_alloc(heap, size, alignment);
   simple_mtx_unlock(&device->as.lock);
   return address;
}

static inline uint64_t
panvk_as_alloc_fixed_address(struct panvk_device *device,
                             struct util_vma_heap *heap, uint64_t address,
                             uint64_t size)
{
   simple_mtx_lock(&device->as.lock);
   bool alloc_result = util_vma_heap_alloc_addr(heap, address, size);
   simple_mtx_unlock(&device->as.lock);
   return alloc_result ? address : 0;
}

static inline void
panvk_as_free(struct panvk_device *device, struct util_vma_heap *heap,
              uint64_t address, uint64_t size)
{
   simple_mtx_lock(&device->as.lock);
   util_vma_heap_free(heap, address, size);
   simple_mtx_unlock(&device->as.lock);
}

struct nir_shader;

bool panvk_nir_lower_tile_image(struct nir_shader *nir,
                                uint32_t *color_read_out, bool *z_read_out,
                                bool *s_read_out);

#if PAN_ARCH
VkResult
panvk_per_arch(create_device)(struct panvk_physical_device *physical_device,
                              const VkDeviceCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkDevice *pDevice);

void panvk_per_arch(destroy_device)(struct panvk_device *device,
                                    const VkAllocationCallbacks *pAllocator);

static inline VkResult
panvk_common_check_status(struct panvk_device *dev)
{
   return vk_check_printf_status(&dev->vk, &dev->printf.ctx);
}

VkResult panvk_per_arch(device_check_status)(struct vk_device *vk_dev);

#if PAN_ARCH >= 10
VkResult panvk_per_arch(init_tiler_oom)(struct panvk_device *device);
#endif
#endif

#endif
