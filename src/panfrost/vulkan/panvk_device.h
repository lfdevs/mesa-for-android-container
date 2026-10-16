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
#include "kmod/panthor_kmod.h"
#ifdef HAVE_PAN_KMOD_KBASE
#include "kmod/kbase_kmod.h"
#endif
#include "util/perf/u_trace.h"

#include "util/simple_mtx.h"
#include "util/u_dynarray.h"
#include "util/u_call_once.h"
#include "util/u_printf.h"
#include "util/vma.h"

/* 128 MiB */
#define PANVK_POLY_HEAP_SIZE (1 << 27)

struct panvk_precomp_cache;
struct panvk_device_draw_context;

enum panvk_queue_family {
   PANVK_QUEUE_FAMILY_GPU,
   PANVK_QUEUE_FAMILY_BIND,
   PANVK_QUEUE_FAMILY_COUNT,
};

enum panvk_va_heap_id {
   PANVK_INVALID_VA_HEAP = -1,
   PANVK_EXEC_VA_HEAP = 0,
   PANVK_NO_EXEC_VA_HEAP,
   PANVK_FIXED_VA_HEAP,
   PANVK_VA_HEAP_COUNT,
};

struct panvk_va_heap {
   struct util_vma_heap heap;
   uint64_t start;
   uint64_t end;
};

struct panvk_device {
   struct vk_device vk;

   struct {
      simple_mtx_t lock;
      struct panvk_va_heap heaps[PANVK_VA_HEAP_COUNT];
   } as;

   struct {
      struct pan_kmod_vm *vm;
      struct pan_kmod_dev *dev;
      struct pan_kmod_allocator allocator;
   } kmod;

   struct panvk_priv_bo *tiler_heap;
   /* v9: second heap for the odd passes of PANVK_OVERLAP (v67b), so each
    * heap grows from its own base (kbase commits up to the faulting page). */
   struct panvk_priv_bo *tiler_heap2;
   struct panvk_priv_bo *indirect_varying_buffer;
   struct panvk_priv_bo *sample_positions;

   struct {
      struct panvk_priv_bo *handlers_bo;
      uint32_t handler_stride;
   } tiler_oom;

   struct vk_meta_device meta;

   /* BC emulation (csf-v11/200): PANVK_DEBUG=bc_timing GPU timestamps,
    * under the lock in panvk_vX_cmd_meta.c. */
   struct {
      VkQueryPool timing_pool;
      uint32_t timing_next;
      uint64_t timing_ops;
      double timing_us;
      /* csf-v11/206: expanded EAC endpoint pair tables (panvk_bc_eac_lut.h)
       * per kind, created at the first transcode that needs one. */
      struct panvk_priv_bo *eac_lut[3];
   } bc;

   /* BC raw store (csf-v11/201), under the lock in panvk_image.c: the
    * spill file (fd + 1, 0: not opened, -1: unavailable), its end, and the
    * spill worker. */
   struct {
      int fd_plus1;
      uint64_t file_end;
      struct util_queue *queue;
      uint64_t resident_B, spilled_B;
      /* csf-v11/207, PANVK_BC_RAW=lazy: set (atomically) at the first read
       * of a BC image that kept no raw blocks; BC images created after it
       * keep a raw store again. */
      int needed;
   } bc_raw;

   /* Bound in place of a missing FS so the depth bounds test still runs. */
   struct panvk_shader *depth_bounds_fs;

   /* Bifrost (v6/v7) has no NULL descriptor. A null descriptor write copies
    * one of these instead: a texture of an internal 1x1 image with a ZERO
    * swizzle, and a storage image and texel buffer whose attribute buffer
    * is 0 bytes, so loads return zeros and stores are dropped by the bounds
    * check. zero_bo is zeroed memory nothing writes, what null vertex
    * buffers and UBOs point at (null SSBOs too, with size 0). Unused, and
    * the same layout, on v9+.
    * From JimVulkan/mesa-unified-drivers (MIT, Copyright 2026 XclipseTools).
    */
#define PANVK_NULL_UBO_ENTRIES 256
   struct {
      bool valid;
      uint32_t tex[8], img[8], texel[8];
      struct panvk_priv_bo *zero_bo;
      uint64_t zero_addr;
      VkImage image;
      VkDeviceMemory image_mem;
      VkImageView view;
   } null_descs;

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
   /* Allocated (and fully committed) at the first gpu_prerast /
    * tessellation draw recorded on the device, under gpu_prerast_lock,
    * unless PANVK_GPU_PRERAST_ARENA=eager (csf-v11/205). Read them through
    * panvk_per_arch(device_gpu_prerast_arena)(). gpu_prerast_wanted /
    * gpu_prerast_tess_wanted say whether the device may use them. */
   struct panvk_priv_mem gpu_prerast_arena;
   struct panvk_priv_mem gpu_prerast_tess_arena;
   /* Second tessellation arena, CSF only, at the first draw that picks it
    * (csf-v11 tess-arenas, see device_gpu_prerast_tess_arena2()). */
   struct panvk_priv_mem gpu_prerast_tess_arena2;
   bool gpu_prerast_wanted;
   bool gpu_prerast_tess_wanted;
   simple_mtx_t gpu_prerast_lock;
   /* struct panvk_gpu_prerast_syncs */
   struct panvk_priv_bo *gpu_prerast_syncs;
   /* CSF tessellation fast paths, read once at device creation so that a
    * process can A/B them per VkDevice. */
   struct {
      /* PANVK_TESS_FUSE (default 1): panlib_tess_count_draw(). */
      bool fuse;
      /* PANVK_TESS_FUSE_GEN_MAX (default 64): patches of a direct draw the
       * fused workgroup also tessellates. */
      uint32_t fuse_gen_max;
      /* PANVK_TESS_SETUP_STEP (default 1): panlib_tess_setup_step(). */
      bool setup_in_step;
      /* PANVK_TESS_ARENAS (default 2): tessellation arenas a command
       * buffer alternates its tess draws over. */
      uint8_t arenas;
      /* PANVK_TESS_WINDING_INLINE (default 1): POLY_TESS_CCW_INLINE. */
      bool winding_inline;
   } tess_opt;

   /* Device-wide thread local storage shared by all command buffers. The
    * last BO is the current one; outgrown ones stay alive for command
    * buffers that still point at them. */
   struct {
      simple_mtx_t lock;
      struct util_dynarray bos;
   } tls;

   struct {
      bool initialized;
      /* Only used on CSF, to synchronize initialization between multiple
       * threads */
      simple_mtx_t init_lock;

      /* Used to propagate errors from poly_heap initialization back to the
       * caller, since the call_once interface does not allow for a fallible
       * initialization function */
      VkResult init_result;

      struct panvk_priv_bo *buffer;

      /* 'struct poly_heap' allocation */
      struct panvk_priv_mem state;
   } poly_heap;

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

/* CSF interface information, sourced from the panthor uAPI or from the
 * kbase GLB interface depending on which backend the device came from.
 * Always use this instead of calling panthor_kmod_get_csif_props()
 * directly, which reads garbage on a kbase device. */
static inline const struct drm_panthor_csif_info *
panvk_get_csif_props(const struct panvk_device *dev)
{
#if defined(HAVE_PAN_KMOD_KBASE) && !defined(HAVE_PAN_KMOD_PANTHOR)
   return kbase_kmod_get_csif_props(dev->kmod.dev);
#elif defined(HAVE_PAN_KMOD_KBASE)
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   if (phys_dev->kbase_node_path[0])
      return kbase_kmod_get_csif_props(dev->kmod.dev);

   return panthor_kmod_get_csif_props(dev->kmod.dev);
#else
   return panthor_kmod_get_csif_props(dev->kmod.dev);
#endif
}

/* Latest cache-flush ID, sourced from the panthor uAPI or the kbase CSF
 * USER register page depending on which backend the device came from. */
static inline uint32_t
panvk_get_flush_id(const struct panvk_device *dev)
{
#if defined(HAVE_PAN_KMOD_KBASE) && !defined(HAVE_PAN_KMOD_PANTHOR)
   return kbase_kmod_get_flush_id(dev->kmod.dev);
#elif defined(HAVE_PAN_KMOD_KBASE)
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   if (phys_dev->kbase_node_path[0])
      return kbase_kmod_get_flush_id(dev->kmod.dev);

   return panthor_kmod_get_flush_id(dev->kmod.dev);
#else
   return panthor_kmod_get_flush_id(dev->kmod.dev);
#endif
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

static inline enum panvk_va_heap_id
panvk_va_heap_fallback(enum panvk_va_heap_id heap)
{
   switch (heap) {
   /* Non-executable objects can live in the executable heap if there's no space
    * left in the non-executable one. */
   case PANVK_NO_EXEC_VA_HEAP:
      return PANVK_EXEC_VA_HEAP;

   case PANVK_FIXED_VA_HEAP:
   case PANVK_EXEC_VA_HEAP:
   default:
      return PANVK_INVALID_VA_HEAP;
   }
}

static inline uint64_t
panvk_as_alloc(struct panvk_device *device, enum panvk_va_heap_id heap,
               uint64_t size, uint64_t alignment)
{
   uint64_t address = 0;

   simple_mtx_lock(&device->as.lock);
   while (address == 0 && heap != PANVK_INVALID_VA_HEAP) {
      address =
         util_vma_heap_alloc(&device->as.heaps[heap].heap, size, alignment);
      heap = panvk_va_heap_fallback(heap);
   }
   simple_mtx_unlock(&device->as.lock);

   return address;
}

static inline uint64_t
panvk_as_alloc_fixed_address(struct panvk_device *device, uint64_t address,
                             uint64_t size)
{
   simple_mtx_lock(&device->as.lock);
   bool alloc_result = util_vma_heap_alloc_addr(
      &device->as.heaps[PANVK_FIXED_VA_HEAP].heap, address, size);
   simple_mtx_unlock(&device->as.lock);
   return alloc_result ? address : 0;
}

static inline void
panvk_as_free(struct panvk_device *device, uint64_t address, uint64_t size)
{
   struct panvk_va_heap *heap = NULL;

   for (uint32_t i = 0; i < ARRAY_SIZE(device->as.heaps); i++) {
      if (address >= device->as.heaps[i].start && address < device->as.heaps[i].end) {
         heap = &device->as.heaps[i];
         break;
      }
   }

   assert(heap);

   simple_mtx_lock(&device->as.lock);
   util_vma_heap_free(&heap->heap, address, size);
   simple_mtx_unlock(&device->as.lock);
}

struct vk_queue;
struct vk_queue_submit;

VkResult panvk_create_bind_queue(struct panvk_device *dev,
                                 const VkDeviceQueueCreateInfo *create_info,
                                 uint32_t queue_idx,
                                 struct vk_queue **out_queue);
void panvk_destroy_bind_queue(struct vk_queue *vk_queue);
VkResult panvk_bind_queue_submit(struct vk_queue *vk_queue,
                                 struct vk_queue_submit *vk_submit);
VkResult panvk_bind_queue_check_status(struct vk_queue *vk_queue);
VkResult panvk_queue_vm_bind(struct vk_queue *vk_queue,
                             struct vk_queue_submit *vk_submit,
                             uint32_t syncobj_handle);

#if PAN_ARCH
VkResult
panvk_per_arch(create_device)(struct panvk_physical_device *physical_device,
                              const VkDeviceCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkDevice *pDevice);

void panvk_per_arch(destroy_device)(struct panvk_device *device,
                                    const VkAllocationCallbacks *pAllocator);

/* Device teardown of the BC emulation state (csf-v11/200), then of the
 * vk_meta device (it replaces panvk_meta_cleanup() in destroy_device). */
void panvk_per_arch(device_bc_finish)(struct panvk_device *dev);

/* The gpu_prerast arena (tess = false) or the tessellation arena,
 * allocated on first use on v10+ (csf-v11/205). Returns a zero
 * panvk_priv_mem if the device does not use it or the allocation failed. */
struct panvk_priv_mem
panvk_per_arch(device_gpu_prerast_arena)(struct panvk_device *dev, bool tess);

/* The second tessellation arena (csf-v11 tess-arenas), allocated on first
 * use like the first one. Zero when tessellation is off, PANVK_TESS_ARENAS
 * is 1 or the allocation failed: the draw then uses the first arena. */
struct panvk_priv_mem
panvk_per_arch(device_gpu_prerast_tess_arena2)(struct panvk_device *dev);

static inline VkResult
panvk_common_check_status(struct panvk_device *dev)
{
   return vk_check_printf_status(&dev->vk, &dev->printf.ctx);
}

VkResult panvk_per_arch(device_check_status)(struct vk_device *vk_dev);

#if PAN_ARCH >= 10
VkResult panvk_per_arch(init_tiler_oom)(struct panvk_device *device);
#endif

VkResult panvk_per_arch(device_init_poly_heap)(struct panvk_device *dev);
#endif

#endif
