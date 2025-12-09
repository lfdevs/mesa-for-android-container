/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVKMD_H
#define NVKMD_H 1

#include "nv_device_info.h"
#include "util/list.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"

#include "../nvk_debug.h"
#include "vulkan/vulkan_core.h"

#include <assert.h>
#include <stdbool.h>
#include <stdbool.h>
#include <sys/types.h>

struct nvkmd_ctx;
struct nvkmd_dev;
struct nvkmd_mem;
struct nvkmd_pdev;
struct nvkmd_va;

struct _drmDevice;
struct vk_object_base;
struct vk_sync_wait;
struct vk_sync_signal;

/*
 * Enums
 */

enum nvkmd_mem_flags {
   /** Place memory as local as possible.
    *
    * This should be the default for most memory allocations.  On discrete
    * GPUs, it will default to be placed in VRAM but may be paged out to GART,
    * depending on system memory pressure.
    */
   NVKMD_MEM_LOCAL      = 1 << 0,

   /** Place the memory in GART */
   NVKMD_MEM_GART       = 1 << 1,

   /** Place the memory in VRAM */
   NVKMD_MEM_VRAM       = 1 << 2,

   /** This memory object may be mapped */
   NVKMD_MEM_CAN_MAP    = 1 << 3,

   /** This memory object may be shared with other processes */
   NVKMD_MEM_SHARED     = 1 << 4,

   /** This memory object has coherent CPU maps */
   NVKMD_MEM_COHERENT   = 1 << 5,
};

#define NVKMD_MEM_PLACEMENT_FLAGS \
   (NVKMD_MEM_LOCAL | NVKMD_MEM_GART | NVKMD_MEM_VRAM)

enum nvkmd_mem_map_flags {
   NVKMD_MEM_MAP_RD     = 1 << 0,
   NVKMD_MEM_MAP_WR     = 1 << 1,
   NVKMD_MEM_MAP_RDWR   = NVKMD_MEM_MAP_RD | NVKMD_MEM_MAP_WR,

   /** Create a client mapping
    *
    * This sets nvkmd_mem::client_map instead of nvkmd_mem::map.  These
    * mappings may be different from internal mappings and have different
    * rules.  Only one client mapping may exist at a time but internal
    * mappings are reference counted.  Only client mappings can be used with
    * MAP_FIXED or unmapped with nvkmd_mem_overmap().
    */
   NVKMD_MEM_MAP_CLIENT = 1 << 2,

   NVKMD_MEM_MAP_FIXED  = 1 << 3,
};

enum nvkmd_va_flags {
   /** This VA should be configured for sparse (soft faults) */
   NVKMD_VA_SPARSE = 1 << 0,

   /** This VA should come from the capture/replay pool */
   NVKMD_VA_REPLAY = 1 << 1,

   /** Attempt to place this VA at the requested address and fail otherwise */
   NVKMD_VA_ALLOC_FIXED = 1 << 2,

   /** This VA will only be used with GART.
    *
    * Alignment requirements for GART-only are lower.
    */
   NVKMD_VA_GART = 1 << 3,
};

enum nvkmd_engines {
   NVKMD_ENGINE_COPY    = 1 << 0,
   NVKMD_ENGINE_2D      = 1 << 1,
   NVKMD_ENGINE_3D      = 1 << 2,
   NVKMD_ENGINE_M2MF    = 1 << 3,
   NVKMD_ENGINE_COMPUTE = 1 << 4,
   NVKMD_ENGINE_BIND    = 1 << 5,
};

enum nvkmd_bind_op {
   NVKMD_BIND_OP_BIND,
   NVKMD_BIND_OP_UNBIND,
};

/*
 * Structs
 */

struct nvkmd_info {
   bool has_dma_buf;
   bool has_get_vram_used;
   bool has_alloc_tiled;
   bool has_map_fixed;
   bool has_overmap;
};

struct nvkmd_pdev_ops {
   void (*destroy)(struct nvkmd_pdev *pdev);

   uint64_t (*get_vram_used)(struct nvkmd_pdev *pdev);

   int (*get_drm_primary_fd)(struct nvkmd_pdev *pdev);

   VkResult (*create_dev)(struct nvkmd_pdev *pdev,
                          struct vk_object_base *log_obj,
                          struct nvkmd_dev **dev_out);
};

struct nvkmd_pdev {
   const struct nvkmd_pdev_ops *ops;

   enum nvk_debug debug_flags;

   struct nv_device_info dev_info;
   struct nvkmd_info kmd_info;

   /** Device-wide GPU memory bind alignment
    *
    * Individual BOs may have a smaller alignment if, for instance, we know
    * they are pinned to system ram.
    */
   uint32_t bind_align_B;

   struct {
      dev_t render_dev;
      dev_t primary_dev;
   } drm;

   const struct vk_sync_type *const *sync_types;
};

struct nvkmd_dev_ops {
   void (*destroy)(struct nvkmd_dev *dev);

   uint64_t (*get_gpu_timestamp)(struct nvkmd_dev *dev);

   int (*get_drm_fd)(struct nvkmd_dev *dev);

   VkResult (*alloc_mem)(struct nvkmd_dev *dev,
                         struct vk_object_base *log_obj,
                         uint64_t size_B, uint64_t align_B,
                         enum nvkmd_mem_flags flags,
                         struct nvkmd_mem **mem_out);

   VkResult (*alloc_tiled_mem)(struct nvkmd_dev *dev,
                               struct vk_object_base *log_obj,
                               uint64_t size_B, uint64_t align_B,
                               uint8_t pte_kind, uint16_t tile_mode,
                               enum nvkmd_mem_flags flags,
                               struct nvkmd_mem **mem_out);

   VkResult (*import_dma_buf)(struct nvkmd_dev *dev,
                              struct vk_object_base *log_obj,
                              int fd, struct nvkmd_mem **mem_out);

   VkResult (*alloc_va)(struct nvkmd_dev *dev,
                        struct vk_object_base *log_obj,
                        enum nvkmd_va_flags flags, uint8_t pte_kind,
                        uint64_t size_B, uint64_t align_B,
                        uint64_t fixed_addr, struct nvkmd_va **va_out);

   VkResult (*create_ctx)(struct nvkmd_dev *dev,
                          struct vk_object_base *log_obj,
                          enum nvkmd_engines engines,
                          struct nvkmd_ctx **ctx_out);
};

struct nvkmd_dev {
   const struct nvkmd_dev_ops *ops;
   struct nvkmd_pdev *pdev;

   /* Start and end of the usable VA space.  All nvkmd_va objects will be
    * allocated within this range.
    */
   uint64_t va_start, va_end;

   struct list_head mems;
   simple_mtx_t mems_mutex;
};

struct nvkmd_mem_ops {
   void (*free)(struct nvkmd_mem *mem);

   VkResult (*map)(struct nvkmd_mem *mem,
                   struct vk_object_base *log_obj,
                   enum nvkmd_mem_map_flags flags,
                   void *fixed_addr,
                   void **map_out);

   void (*unmap)(struct nvkmd_mem *mem,
                 enum nvkmd_mem_map_flags flags,
                 void *map);

   VkResult (*overmap)(struct nvkmd_mem *mem,
                       struct vk_object_base *log_obj,
                       enum nvkmd_mem_map_flags flags,
                       void *map);

   void (*sync_to_gpu)(struct nvkmd_mem *mem,
                       uint64_t offset_B, uint64_t range_B);

   void (*sync_from_gpu)(struct nvkmd_mem *mem,
                         uint64_t offset_B, uint64_t range_B);

   VkResult (*export_dma_buf)(struct nvkmd_mem *mem,
                              struct vk_object_base *log_obj,
                              int *fd_out);

   /** Handle to use for NVK_DEBUG_VM logging */
   uint32_t (*log_handle)(struct nvkmd_mem *mem);
};

struct nvkmd_mem {
   const struct nvkmd_mem_ops *ops;
   struct nvkmd_dev *dev;

   /* Optional link in nvkmd_dev::mems */
   struct list_head link;

   uint32_t refcnt;

   enum nvkmd_mem_flags flags;
   uint32_t bind_align_B;

   uint64_t size_B;
   struct nvkmd_va *va;

   simple_mtx_t map_mutex;
   uint32_t map_cnt;
   void *map;

   void *client_map;
};

void nvkmd_mem_init(struct nvkmd_dev *dev,
                    struct nvkmd_mem *mem,
                    const struct nvkmd_mem_ops *ops,
                    enum nvkmd_mem_flags flags,
                    uint64_t size_B,
                    uint32_t bind_align_B);

struct nvkmd_va_ops {
   void (*free)(struct nvkmd_va *va);

   VkResult (*bind_mem)(struct nvkmd_va *va,
                        struct vk_object_base *log_obj,
                        uint64_t va_offset_B,
                        struct nvkmd_mem *mem,
                        uint64_t mem_offset_B,
                        uint64_t range_B);

   VkResult (*unbind)(struct nvkmd_va *va,
                      struct vk_object_base *log_obj,
                      uint64_t va_offset_B,
                      uint64_t range_B);
};

struct nvkmd_va {
   const struct nvkmd_va_ops *ops;
   struct nvkmd_dev *dev;

   enum nvkmd_va_flags flags;
   uint8_t pte_kind;
   uint64_t addr;
   uint64_t size_B;
};

struct nvkmd_ctx_exec {
   uint64_t addr;
   uint32_t size_B;
   /* True if this push ends in an incomplete method and requires the next
    * push to provide the method data.  In this case, this push and the next
    * one must be in the same submit ioctl.
    */
   bool incomplete;
   bool no_prefetch;
};

struct nvkmd_ctx_bind {
   enum nvkmd_bind_op op;

   struct nvkmd_va *va;
   uint64_t va_offset_B;

   /* Ignored if op != NVK_BIND_OP_UNBIND */
   struct nvkmd_mem *mem;
   uint64_t mem_offset_B;

   uint64_t range_B;
};

struct nvkmd_ctx_ops {
   void (*destroy)(struct nvkmd_ctx *ctx);

   VkResult (*wait)(struct nvkmd_ctx *ctx,
                    struct vk_object_base *log_obj,
                    uint32_t wait_count,
                    const struct vk_sync_wait *waits);

   VkResult (*exec)(struct nvkmd_ctx *ctx,
                    struct vk_object_base *log_obj,
                    uint32_t exec_count,
                    const struct nvkmd_ctx_exec *execs);

   VkResult (*bind)(struct nvkmd_ctx *ctx,
                    struct vk_object_base *log_obj,
                    uint32_t bind_count,
                    const struct nvkmd_ctx_bind *binds);

   /* Implies flush() */
   VkResult (*signal)(struct nvkmd_ctx *ctx,
                      struct vk_object_base *log_obj,
                      uint32_t signal_count,
                      const struct vk_sync_signal *signals);

   VkResult (*flush)(struct nvkmd_ctx *ctx,
                     struct vk_object_base *log_obj);

   /* Implies flush() */
   VkResult (*sync)(struct nvkmd_ctx *ctx,
                    struct vk_object_base *log_obj);
};

struct nvkmd_ctx {
   const struct nvkmd_ctx_ops *ops;
   struct nvkmd_dev *dev;
};

/*
 * Macros
 *
 * All subclassed structs must be named nvkmd_<subcls>_<strct> where the
 * original struct is named nvkmd_<strct>
 */

#define NVKMD_DECL_SUBCLASS(strct, subcls)                                 \
   extern const struct nvkmd_##strct##_ops nvkmd_##subcls##_##strct##_ops; \
   static inline struct nvkmd_##subcls##_##strct *                         \
   nvkmd_##subcls##_##strct(struct nvkmd_##strct *nvkmd)                   \
   {                                                                       \
      assert(nvkmd->ops == &nvkmd_##subcls##_##strct##_ops);               \
      return container_of(nvkmd, struct nvkmd_##subcls##_##strct, base);   \
   }

/*
 * Methods
 *
 * Even though everything goes through a function pointer table, we always add
 * an inline wrapper in case we want to move something into "core" NVKMD.
 */

VkResult MUST_CHECK
nvkmd_try_create_pdev_for_drm(struct _drmDevice *drm_device,
                              struct vk_object_base *log_obj,
                              enum nvk_debug debug_flags,
                              struct nvkmd_pdev **pdev_out);

static inline void
nvkmd_pdev_destroy(struct nvkmd_pdev *pdev)
{
   pdev->ops->destroy(pdev);
}

static inline uint64_t
nvkmd_pdev_get_vram_used(struct nvkmd_pdev *pdev)
{
   return pdev->ops->get_vram_used(pdev);
}

static inline int
nvkmd_pdev_get_drm_primary_fd(struct nvkmd_pdev *pdev)
{
   if (pdev->ops->get_drm_primary_fd == NULL)
      return -1;

   return pdev->ops->get_drm_primary_fd(pdev);
}

static inline VkResult MUST_CHECK
nvkmd_pdev_create_dev(struct nvkmd_pdev *pdev,
                      struct vk_object_base *log_obj,
                      struct nvkmd_dev **dev_out)
{
   return pdev->ops->create_dev(pdev, log_obj, dev_out);
}

static inline void
nvkmd_dev_destroy(struct nvkmd_dev *dev)
{
   dev->ops->destroy(dev);
}

static inline uint64_t
nvkmd_dev_get_gpu_timestamp(struct nvkmd_dev *dev)
{
   return dev->ops->get_gpu_timestamp(dev);
}

static inline int
nvkmd_dev_get_drm_fd(struct nvkmd_dev *dev)
{
   if (dev->ops->get_drm_fd == NULL)
      return -1;

   return dev->ops->get_drm_fd(dev);
}

VkResult MUST_CHECK
nvkmd_dev_alloc_mem(struct nvkmd_dev *dev,
                    struct vk_object_base *log_obj,
                    uint64_t size_B, uint64_t align_B,
                    enum nvkmd_mem_flags flags,
                    struct nvkmd_mem **mem_out);

VkResult MUST_CHECK
nvkmd_dev_alloc_tiled_mem(struct nvkmd_dev *dev,
                          struct vk_object_base *log_obj,
                          uint64_t size_B, uint64_t align_B,
                          uint8_t pte_kind, uint16_t tile_mode,
                          enum nvkmd_mem_flags flags,
                          struct nvkmd_mem **mem_out);

/* Implies NVKMD_MEM_CAN_MAP */
VkResult MUST_CHECK
nvkmd_dev_alloc_mapped_mem(struct nvkmd_dev *dev,
                           struct vk_object_base *log_obj,
                           uint64_t size_B, uint64_t align_B,
                           enum nvkmd_mem_flags flags,
                           enum nvkmd_mem_map_flags map_flags,
                           struct nvkmd_mem **mem_out);

VkResult MUST_CHECK
nvkmd_dev_import_dma_buf(struct nvkmd_dev *dev,
                         struct vk_object_base *log_obj,
                         int fd, struct nvkmd_mem **mem_out);

struct nvkmd_mem *
nvkmd_dev_lookup_mem_by_va(struct nvkmd_dev *dev,
                           uint64_t addr,
                           uint64_t *offset_out);

VkResult MUST_CHECK
nvkmd_dev_alloc_va(struct nvkmd_dev *dev,
                   struct vk_object_base *log_obj,
                   enum nvkmd_va_flags flags, uint8_t pte_kind,
                   uint64_t size_B, uint64_t align_B,
                   uint64_t fixed_addr, struct nvkmd_va **va_out);

static inline VkResult MUST_CHECK
nvkmd_dev_create_ctx(struct nvkmd_dev *dev,
                     struct vk_object_base *log_obj,
                     enum nvkmd_engines engines,
                     struct nvkmd_ctx **ctx_out)
{
   return dev->ops->create_ctx(dev, log_obj, engines, ctx_out);
}

static inline struct nvkmd_mem *
nvkmd_mem_ref(struct nvkmd_mem *mem)
{
   p_atomic_inc(&mem->refcnt);
   return mem;
}

void nvkmd_mem_unref(struct nvkmd_mem *mem);

VkResult MUST_CHECK
nvkmd_mem_map(struct nvkmd_mem *mem, struct vk_object_base *log_obj,
              enum nvkmd_mem_map_flags flags, void *fixed_addr,
              void **map_out);

void nvkmd_mem_unmap(struct nvkmd_mem *mem, enum nvkmd_mem_map_flags flags);

static inline VkResult MUST_CHECK
nvkmd_mem_overmap(struct nvkmd_mem *mem, struct vk_object_base *log_obj,
                  enum nvkmd_mem_map_flags flags)
{
   assert(flags & NVKMD_MEM_MAP_CLIENT);
   assert(mem->client_map != NULL);

   VkResult result = mem->ops->overmap(mem, log_obj, flags, mem->client_map);
   if (result == VK_SUCCESS)
      mem->client_map = NULL;

   return result;
}

void nvkmd_mem_sync_to_gpu(struct nvkmd_mem *mem, bool client_map,
                           uint64_t offset_B, uint64_t range_B);
void nvkmd_mem_sync_from_gpu(struct nvkmd_mem *mem, bool client_map,
                             uint64_t offset_B, uint64_t range_B);

static inline void
nvkmd_mem_sync_map_to_gpu(struct nvkmd_mem *mem,
                          uint64_t offset_B, uint64_t range_B)
{
   nvkmd_mem_sync_to_gpu(mem, false, offset_B, range_B);
}

static inline void
nvkmd_mem_sync_client_map_to_gpu(struct nvkmd_mem *mem,
                                 uint64_t offset_B, uint64_t range_B)
{
   nvkmd_mem_sync_to_gpu(mem, true, offset_B, range_B);
}

static inline void
nvkmd_mem_sync_map_from_gpu(struct nvkmd_mem *mem,
                            uint64_t offset_B, uint64_t range_B)
{
   nvkmd_mem_sync_from_gpu(mem, false, offset_B, range_B);
}

static inline void
nvkmd_mem_sync_client_map_from_gpu(struct nvkmd_mem *mem,
                                   uint64_t offset_B, uint64_t range_B)
{
   nvkmd_mem_sync_from_gpu(mem, true, offset_B, range_B);
}

static inline VkResult MUST_CHECK
nvkmd_mem_export_dma_buf(struct nvkmd_mem *mem,
                      struct vk_object_base *log_obj,
                      int *fd_out)
{
   assert(mem->flags & NVKMD_MEM_SHARED);

   return mem->ops->export_dma_buf(mem, log_obj, fd_out);
}

void
nvkmd_va_free(struct nvkmd_va *va);

VkResult MUST_CHECK
nvkmd_va_bind_mem(struct nvkmd_va *va,
                  struct vk_object_base *log_obj,
                  uint64_t va_offset_B,
                  struct nvkmd_mem *mem,
                  uint64_t mem_offset_B,
                  uint64_t range_B);

VkResult MUST_CHECK
nvkmd_va_unbind(struct nvkmd_va *va,
                struct vk_object_base *log_obj,
                uint64_t va_offset_B,
                uint64_t range_B);

static inline void
nvkmd_ctx_destroy(struct nvkmd_ctx *ctx)
{
   ctx->ops->destroy(ctx);
}

static inline VkResult MUST_CHECK
nvkmd_ctx_wait(struct nvkmd_ctx *ctx,
               struct vk_object_base *log_obj,
               uint32_t wait_count,
               const struct vk_sync_wait *waits)
{
   return ctx->ops->wait(ctx, log_obj, wait_count, waits);
}

VkResult MUST_CHECK
nvkmd_ctx_exec(struct nvkmd_ctx *ctx,
               struct vk_object_base *log_obj,
               uint32_t exec_count,
               const struct nvkmd_ctx_exec *execs);

VkResult MUST_CHECK
nvkmd_ctx_bind(struct nvkmd_ctx *ctx,
               struct vk_object_base *log_obj,
               uint32_t bind_count,
               const struct nvkmd_ctx_bind *binds);

static inline VkResult MUST_CHECK
nvkmd_ctx_signal(struct nvkmd_ctx *ctx,
                 struct vk_object_base *log_obj,
                 uint32_t signal_count,
                 const struct vk_sync_signal *signals)
{
   return ctx->ops->signal(ctx, log_obj, signal_count, signals);
}

static inline VkResult MUST_CHECK
nvkmd_ctx_flush(struct nvkmd_ctx *ctx,
                struct vk_object_base *log_obj)
{
   return ctx->ops->flush(ctx, log_obj);
}

static inline VkResult MUST_CHECK
nvkmd_ctx_sync(struct nvkmd_ctx *ctx,
               struct vk_object_base *log_obj)
{
   return ctx->ops->sync(ctx, log_obj);
}

#endif /* NVKMD_H */
