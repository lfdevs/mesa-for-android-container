/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <xf86drm.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include "util/hash_table.h"
#include "util/macros.h"
#include "util/stack_array.h"
#include "util/timespec.h"

#include "drm-uapi/panfrost_drm.h"

#include "pan_kmod_backend.h"

#include "pan_props.h"

/* Maximum kmod BO label length, including NUL-terminator */
#define PANFROST_BO_LABEL_MAXLEN 4096

const struct pan_kmod_ops panfrost_kmod_ops;

struct panfrost_kmod_vm {
   struct pan_kmod_vm base;
};

struct panfrost_kmod_dev {
   struct pan_kmod_dev base;
   struct panfrost_kmod_vm *vm;
};

struct panfrost_kmod_bo {
   struct pan_kmod_bo base;

   /* This is actually the VA assigned to the BO at creation/import time.
    * We don't control it, it's automatically assigned by the kernel driver.
    */
   uint64_t offset;
};

enum panfrost_kmod_perf_session_state {
   PANFROST_KMOD_PERF_SESSION_STOPPED,
   PANFROST_KMOD_PERF_SESSION_STARTING,
   PANFROST_KMOD_PERF_SESSION_STARTED,
   PANFROST_KMOD_PERF_SESSION_STOPPING,
};

struct panfrost_kmod_perf_session {
   struct pan_kmod_perf_session base;
   enum panfrost_kmod_perf_session_state state;
   thrd_t thread;

   /* We can't use a simple_mtx_t here, because the lock is taken by both an RT
    * thread and a normal prio one, and we want priority inheritance when the
    * normal prio thread has the lock held and the RT thread wants to acquire it.
    */
   pthread_mutex_t lock;
   struct pan_kmod_perf_sample_layout sample_layout;

   int timerfd;
   int eventfd;

   void *hw_sample;
   struct pan_kmod_perf_consolidated_sample consolidated_sample;
   struct pan_kmod_perf_dumped_sample dumped_sample;
};

/* Abstraction over the raw drm_panfrost_get_param ioctl for fetching
 * information about devices.
 */
static __u64
panfrost_query_raw(int fd, enum drm_panfrost_param param, bool required,
                   unsigned default_value)
{
   struct drm_panfrost_get_param get_param = {};
   ASSERTED int ret;

   get_param.param = param;
   ret = pan_kmod_ioctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &get_param);

   if (ret) {
      assert(!required);
      return default_value;
   }

   return get_param.value;
}

static void
panfrost_dev_query_thread_props(struct panfrost_kmod_dev *panfrost_dev)
{
   struct pan_kmod_dev_props *props = &panfrost_dev->base.props;
   const struct pan_kmod_dev *dev = &panfrost_dev->base;
   int fd = dev->fd;

   props->max_threads_per_core =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_MAX_THREADS, true, 0);
   if (!props->max_threads_per_core) {
      switch (pan_arch(props->gpu_id)) {
      case 4:
      case 5:
         props->max_threads_per_core = 256;
         break;

      case 6:
         /* Bifrost, first generation */
         props->max_threads_per_core = 384;
         break;

      case 7:
         /* Bifrost, second generation (G31 is 512 but it doesn't matter) */
         props->max_threads_per_core = 768;
         break;

      case 9:
         /* Valhall, first generation. */
         props->max_threads_per_core = 512;
         break;

      default:
         assert(!"Unsupported arch");
      }
   }

   props->max_threads_per_wg = panfrost_query_raw(
      fd, DRM_PANFROST_PARAM_THREAD_MAX_WORKGROUP_SZ, true, 0);
   if (!props->max_threads_per_wg)
      props->max_threads_per_wg = props->max_threads_per_core;

   uint32_t thread_features =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_THREAD_FEATURES, true, 0);
   props->max_tasks_per_core = MAX2(thread_features >> 24, 1);
   props->num_registers_per_core = thread_features & 0xffff;
   if (!props->num_registers_per_core) {
      switch (pan_arch(props->gpu_id)) {
      case 4:
      case 5:
         /* Assume we can always schedule max_threads_per_core when using 4
          * registers per-shader or less.
          */
         props->num_registers_per_core = props->max_threads_per_core * 4;
         break;

      case 6:
         /* Assume we can always schedule max_threads_per_core for shader
          * using the full per-shader register file (64 regs).
          */
         props->num_registers_per_core = props->max_threads_per_core * 64;
         break;

      case 7:
      case 9:
         /* Assume we can always schedule max_threads_per_core for shaders
          * using half the per-shader register file (32 regs).
          */
         props->num_registers_per_core = props->max_threads_per_core * 32;
         break;

      default:
         assert(!"Unsupported arch");
      }
   }

   props->max_tls_instance_per_core =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_THREAD_TLS_ALLOC, true, 0);
   if (!props->max_tls_instance_per_core)
      props->max_tls_instance_per_core = props->max_threads_per_core;
}

static void
panfrost_dev_query_props(struct panfrost_kmod_dev *panfrost_dev)
{
   struct pan_kmod_dev_props *props = &panfrost_dev->base.props;
   const struct pan_kmod_dev *dev = &panfrost_dev->base;
   int fd = dev->fd;

   memset(props, 0, sizeof(*props));
   props->gpu_id =
      (panfrost_query_raw(fd, DRM_PANFROST_PARAM_GPU_PROD_ID, true, 0) << 16) |
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_GPU_REVISION, true, 0);
   props->shader_present =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_SHADER_PRESENT, true, 0);
   props->tiler_features =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_TILER_FEATURES, true, 0);
   props->mem_features =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_MEM_FEATURES, true, 0);
   props->mmu_features =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_MMU_FEATURES, true, 0);
   props->l2_features =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_L2_FEATURES, true, 0);

   for (unsigned i = 0; i < ARRAY_SIZE(props->texture_features); i++) {
      props->texture_features[i] = panfrost_query_raw(
         fd, DRM_PANFROST_PARAM_TEXTURE_FEATURES0 + i, true, 0);
   }

   props->afbc_features =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_AFBC_FEATURES, true, 0);

   panfrost_dev_query_thread_props(panfrost_dev);

   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 3)) {
      props->gpu_can_query_timestamp = true;
      props->timestamp_frequency = panfrost_query_raw(
         fd, DRM_PANFROST_PARAM_SYSTEM_TIMESTAMP_FREQUENCY, true, 0);

      if (props->timestamp_frequency) {
         props->timestamp_cycles_to_ns_factor =
            (double)NSEC_PER_SEC / props->timestamp_frequency;
      }
   }

   /* Device coherent timestamps are always enabled on panfrost */
   props->timestamp_device_coherent = true;

   /* Support for priorities was added in panfrost 1.5, assumes default
    * priority as medium if the param doesn't exist. */
   uint64_t prios =
      panfrost_query_raw(fd, DRM_PANFROST_PARAM_ALLOWED_JM_CTX_PRIORITIES,
                         false, BITFIELD_BIT(PANFROST_JM_CTX_PRIORITY_MEDIUM));

   if (prios & BITFIELD_BIT(PANFROST_JM_CTX_PRIORITY_LOW))
      props->allowed_group_priorities_mask |= PAN_KMOD_GROUP_ALLOW_PRIORITY_LOW;
   if (prios & BITFIELD_BIT(PANFROST_JM_CTX_PRIORITY_MEDIUM))
      props->allowed_group_priorities_mask |=
         PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;
   if (prios & BITFIELD_BIT(PANFROST_JM_CTX_PRIORITY_HIGH))
      props->allowed_group_priorities_mask |=
         PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH;

   props->supported_bo_flags = PAN_KMOD_BO_FLAG_EXECUTABLE |
                               PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT |
                               PAN_KMOD_BO_FLAG_NO_MMAP;

   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 6)) {
      uint32_t selected_coherency =
         panfrost_query_raw(fd, DRM_PANFROST_PARAM_SELECTED_COHERENCY, true,
                            DRM_PANFROST_GPU_COHERENCY_NONE);

      props->supported_bo_flags |= PAN_KMOD_BO_FLAG_WB_MMAP;
      props->is_io_coherent =
         selected_coherency != DRM_PANFROST_GPU_COHERENCY_NONE;
   }

   props->pgsize_bitmap = PAN_PGSIZE_4K | PAN_PGSIZE_2M;
}

static struct pan_kmod_dev *
panfrost_kmod_dev_create(int fd, uint32_t flags,
                         const struct pan_kmod_driver *drv_info,
                         const struct pan_kmod_allocator *allocator)
{
   if (!pan_kmod_driver_version_at_least(drv_info, 1, 1)) {
      mesa_loge("kernel driver is too old (requires at least 1.1, found %d.%d)",
                drv_info->version.major, drv_info->version.minor);
      return NULL;
   }

   struct panfrost_kmod_dev *panfrost_dev =
      pan_kmod_alloc(allocator, sizeof(*panfrost_dev));
   if (!panfrost_dev) {
      mesa_loge("failed to allocate a panfrost_kmod_dev object");
      return NULL;
   }

   pan_kmod_dev_init(&panfrost_dev->base, fd, flags, drv_info,
                     &panfrost_kmod_ops, allocator);
   panfrost_dev_query_props(panfrost_dev);

   return &panfrost_dev->base;
}

static void
panfrost_kmod_dev_destroy(struct pan_kmod_dev *dev)
{
   struct panfrost_kmod_dev *panfrost_dev =
      container_of(dev, struct panfrost_kmod_dev, base);

   pan_kmod_dev_cleanup(dev);
   pan_kmod_free(dev->allocator, panfrost_dev);
}

static uint32_t
to_panfrost_bo_flags(struct pan_kmod_dev *dev, uint32_t flags)
{
   uint32_t panfrost_flags = 0;

   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 1)) {
      /* The alloc-on-fault feature is only used for the tiler HEAP object,
       * hence the name of the flag on panfrost.
       */
      if (flags & PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT)
         panfrost_flags |= PANFROST_BO_HEAP;

      if (!(flags & PAN_KMOD_BO_FLAG_EXECUTABLE))
         panfrost_flags |= PANFROST_BO_NOEXEC;
   }

   if (flags & PAN_KMOD_BO_FLAG_WB_MMAP) {
      assert(!(flags & PAN_KMOD_BO_FLAG_NO_MMAP));
      panfrost_flags |= PANFROST_BO_WB_MMAP;
   }

   return panfrost_flags;
}

static struct pan_kmod_bo *
panfrost_kmod_bo_alloc(struct pan_kmod_dev *dev,
                       struct pan_kmod_vm *exclusive_vm, uint64_t size,
                       uint32_t flags)
{
   /* The ioctl uses u32 for size. */
   if ((uint64_t)(uint32_t)size != size)
      return NULL;

   /* We can't map GPU uncached. */
   if (flags & PAN_KMOD_BO_FLAG_GPU_UNCACHED)
      return NULL;

   struct panfrost_kmod_bo *bo = pan_kmod_dev_alloc(dev, sizeof(*bo));
   if (!bo)
      return NULL;

   struct drm_panfrost_create_bo req = {
      .size = size,
      .flags = to_panfrost_bo_flags(dev, flags),
   };

   int ret = pan_kmod_ioctl(dev->fd, DRM_IOCTL_PANFROST_CREATE_BO, &req);
   if (ret) {
      mesa_loge("DRM_IOCTL_PANFROST_CREATE_BO failed (err=%d)", errno);
      goto err_free_bo;
   }

   pan_kmod_bo_init(&bo->base, dev, exclusive_vm, req.size, flags, req.handle);
   bo->offset = req.offset;
   return &bo->base;

err_free_bo:
   pan_kmod_dev_free(dev, bo);
   return NULL;
}

static void
panfrost_kmod_bo_free(struct pan_kmod_bo *bo)
{
   pan_kmod_bo_cleanup(bo);
   drmCloseBufferHandle(bo->dev->fd, bo->handle);
   pan_kmod_dev_free(bo->dev, bo);
}

static struct pan_kmod_bo *
panfrost_kmod_bo_import(struct pan_kmod_dev *dev, uint32_t handle,
                        uint64_t size)
{
   struct panfrost_kmod_bo *panfrost_bo =
      pan_kmod_dev_alloc(dev, sizeof(*panfrost_bo));
   if (!panfrost_bo) {
      mesa_loge("failed to allocate a panfrost_kmod_bo object");
      return NULL;
   }

   struct drm_panfrost_get_bo_offset get_bo_offset = {.handle = handle, 0};
   int ret =
      pan_kmod_ioctl(dev->fd, DRM_IOCTL_PANFROST_GET_BO_OFFSET,
                     &get_bo_offset);
   if (ret) {
      mesa_loge("DRM_IOCTL_PANFROST_GET_BO_OFFSET failed (err=%d)", errno);
      goto err_free_bo;
   }

   panfrost_bo->offset = get_bo_offset.offset;

   uint32_t flags = PAN_KMOD_BO_FLAG_IMPORTED;
   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 6)) {
      struct drm_panfrost_query_bo_info args = {
         .handle = handle,
      };

      ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_QUERY_BO_INFO, &args);
      if (ret) {
         mesa_loge("PANFROST_BO_QUERY_INFO failed (err=%d)", errno);
         goto err_free_bo;
      }

      /* FIXME: If the BO comes from a different subsystem
       * (args.extra_flags & DRM_PANTHOR_BO_IS_IMPORTED), we should normally
       * add extra DMA_BUF_IOCTL_SYNC calls around CPU accesses to ensure the
       * CPU mapping consistency, but this is something we never worried about
       * (we've always assumed exporters were exposing uncached mappings with
       * NOP {begin,end}_cpu_access() implementations), and it worked fine until
       * now.
       * The long term plan is to hook up DMA_BUF_IOCTL_SYNC, but this requires
       * more work.
       */
   }

   pan_kmod_bo_init(&panfrost_bo->base, dev, NULL, size, flags, handle);
   return &panfrost_bo->base;

err_free_bo:
   pan_kmod_dev_free(dev, panfrost_bo);
   return NULL;
}

static off_t
panfrost_kmod_bo_get_mmap_offset(struct pan_kmod_bo *bo)
{
   struct drm_panfrost_mmap_bo mmap_bo = {.handle = bo->handle};
   int ret = pan_kmod_ioctl(bo->dev->fd, DRM_IOCTL_PANFROST_MMAP_BO,
                            &mmap_bo);
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_PANFROST_MMAP_BO failed: %m\n");
      assert(0);
   }

   return mmap_bo.offset;
}

static bool
panfrost_kmod_bo_wait(struct pan_kmod_bo *bo, int64_t timeout_ns,
                      bool for_read_only_access)
{
   struct drm_panfrost_wait_bo req = {
      .handle = bo->handle,
      .timeout_ns = timeout_ns,
   };

   /* The ioctl returns >= 0 value when the BO we are waiting for is ready
    * -1 otherwise.
    */
   if (pan_kmod_ioctl(bo->dev->fd, DRM_IOCTL_PANFROST_WAIT_BO, &req) != -1)
      return true;

   assert(errno == ETIMEDOUT || errno == EBUSY);
   return false;
}

static int
panfrost_kmod_flush_bo_map_syncs(struct pan_kmod_dev *dev)
{
   STACK_ARRAY(struct drm_panfrost_bo_sync_op, panfrost_ops,
               util_dynarray_num_elements(&dev->pending_bo_syncs.array,
                                          struct pan_kmod_deferred_bo_sync));

   uint32_t panfrost_count = 0;
   util_dynarray_foreach(&dev->pending_bo_syncs.array,
                         struct pan_kmod_deferred_bo_sync, sync) {
      panfrost_ops[panfrost_count++] = (struct drm_panfrost_bo_sync_op){
         .handle = sync->bo->handle,
         .type = sync->type == PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH
                    ? PANFROST_BO_SYNC_CPU_CACHE_FLUSH
                    : PANFROST_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE,
         .offset = sync->start,
         .size = sync->size,
      };
   }

   struct drm_panfrost_sync_bo req = {
      .ops = (uintptr_t)panfrost_ops,
      .op_count = panfrost_count,
   };
   int ret = pan_kmod_ioctl(dev->fd, DRM_IOCTL_PANFROST_SYNC_BO, &req);
   if (ret)
      mesa_loge("DRM_IOCTL_PANFROST_BO_SYNC failed (err=%d)", errno);

   STACK_ARRAY_FINISH(panfrost_ops);

   return ret;
}

static void
panfrost_kmod_bo_make_evictable(struct pan_kmod_bo *bo)
{
   struct drm_panfrost_madvise req = {
      .handle = bo->handle,
      .madv = PANFROST_MADV_DONTNEED,
   };

   pan_kmod_ioctl(bo->dev->fd, DRM_IOCTL_PANFROST_MADVISE, &req);
}

static bool
panfrost_kmod_bo_make_unevictable(struct pan_kmod_bo *bo)
{
   struct drm_panfrost_madvise req = {
      .handle = bo->handle,
      .madv = PANFROST_MADV_WILLNEED,
   };

   if (pan_kmod_ioctl(bo->dev->fd, DRM_IOCTL_PANFROST_MADVISE, &req) == 0 &&
       req.retained == 0)
      return false;

   return true;
}

/* The VA range is restricted by the kernel driver. Lower 32MB are reserved, and
 * the address space is limited to 32-bit.
 */
#define PANFROST_KMOD_VA_START 0x2000000ull
#define PANFROST_KMOD_VA_END   (1ull << 32)

static struct pan_kmod_va_range
panfrost_kmod_dev_query_user_va_range(const struct pan_kmod_dev *dev)
{
   return (struct pan_kmod_va_range){
      .start = PANFROST_KMOD_VA_START,
      .size = PANFROST_KMOD_VA_END - PANFROST_KMOD_VA_START,
   };
}

static struct pan_kmod_vm *
panfrost_kmod_vm_create(struct pan_kmod_dev *dev, uint32_t flags,
                        uint64_t va_start, uint64_t va_range)
{
   struct panfrost_kmod_dev *panfrost_dev =
      container_of(dev, struct panfrost_kmod_dev, base);

   /* Only one VM per device. */
   if (panfrost_dev->vm) {
      mesa_loge("panfrost_kmod only supports one VM per device");
      return NULL;
   }

   /* Panfrost kernel driver doesn't support userspace VA management. */
   if (!(flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      mesa_loge("panfrost_kmod only supports PAN_KMOD_VM_FLAG_AUTO_VA");
      assert(0);
      return NULL;
   }

   struct panfrost_kmod_vm *vm = pan_kmod_dev_alloc(dev, sizeof(*vm));
   if (!vm) {
      mesa_loge("failed to allocate a panfrost_kmod_vm object");
      return NULL;
   }

   pan_kmod_vm_init(&vm->base, dev, 0, flags);
   panfrost_dev->vm = vm;
   return &vm->base;
}

static void
panfrost_kmod_vm_destroy(struct pan_kmod_vm *vm)
{
   struct panfrost_kmod_dev *panfrost_dev =
      container_of(vm->dev, struct panfrost_kmod_dev, base);

   panfrost_dev->vm = NULL;
   pan_kmod_vm_cleanup(vm);
   pan_kmod_dev_free(vm->dev, vm);
}

static int
panfrost_kmod_vm_bind(struct pan_kmod_vm *vm, enum pan_kmod_vm_op_mode mode,
                      struct pan_kmod_vm_op *ops, uint32_t op_count)
{
   UNUSED struct panfrost_kmod_vm *panfrost_vm =
      container_of(vm, struct panfrost_kmod_vm, base);

   /* We only support IMMEDIATE and WAIT_IDLE mode. Actually we always do
    * WAIT_IDLE in practice, but it shouldn't matter.
    */
   if (mode != PAN_KMOD_VM_OP_MODE_IMMEDIATE &&
       mode != PAN_KMOD_VM_OP_MODE_DEFER_TO_NEXT_IDLE_POINT) {
      mesa_loge("panfrost_kmod doesn't support mode=%d", mode);
      assert(0);
      return -1;
   }

   for (uint32_t i = 0; i < op_count; i++) {

      if (ops[i].type == PAN_KMOD_VM_OP_TYPE_MAP) {
         struct panfrost_kmod_bo *panfrost_bo =
            container_of(ops[i].map.bo, struct panfrost_kmod_bo, base);

         /* Panfrost kernel driver doesn't support userspace VA management. */
         if (ops[i].va.start != PAN_KMOD_VM_MAP_AUTO_VA) {
            mesa_loge("panfrost_kmod can only do auto-VA allocation");
            assert(0);
            return -1;
         }

         /* Panfrost kernel driver only support full BO mapping. */
         if (ops[i].map.bo_offset != 0 ||
             ops[i].va.size != ops[i].map.bo->size) {
            mesa_loge("panfrost_kmod doesn't support partial BO mapping");
            assert(0);
            return -1;
         }

         if (ops[i].flags & PAN_KMOD_VM_OP_OP_MAP_SPARSE) {
            mesa_loge("panfrost_kmod doesn't support sparse mappings");
            assert(0);
            return -1;
         }

         ops[i].va.start = panfrost_bo->offset;
      } else if (ops[i].type == PAN_KMOD_VM_OP_TYPE_UNMAP) {
         /* Do nothing, unmapping is done at BO destruction time. */
      } else {
         /* We reject PAN_KMOD_VM_OP_TYPE_SYNC_ONLY as this implies
          * supporting PAN_KMOD_VM_OP_MODE_ASYNC, which we don't support.
          */
         mesa_loge("panfrost_kmod doesn't support op=%d", ops[i].type);
         assert(0);
         return -1;
      }
   }

   return 0;
}

static uint64_t
panfrost_kmod_query_timestamp(const struct pan_kmod_dev *dev)
{
   return panfrost_query_raw(dev->fd, DRM_PANFROST_PARAM_SYSTEM_TIMESTAMP,
                             false, 0);
}

static void
panfrost_kmod_bo_label(struct pan_kmod_dev *dev, struct pan_kmod_bo *bo, const char *label)
{
   char truncated_label[PANFROST_BO_LABEL_MAXLEN];

   if (!pan_kmod_driver_version_at_least(&dev->driver, 1, 4))
      return;

   if (strnlen(label, PANFROST_BO_LABEL_MAXLEN) == PANFROST_BO_LABEL_MAXLEN) {
      strncpy(truncated_label, label, PANFROST_BO_LABEL_MAXLEN - 1);
      truncated_label[PANFROST_BO_LABEL_MAXLEN - 1] = '\0';
      label = truncated_label;
   }

   struct drm_panfrost_set_label_bo set_label =
      (struct drm_panfrost_set_label_bo) {
      .handle = bo->handle,
      .label = (uint64_t)(uintptr_t)label,
   };

   int ret =
      pan_kmod_ioctl(dev->fd, DRM_IOCTL_PANFROST_SET_LABEL_BO,
                     &set_label);
   if (ret)
      mesa_loge("DRM_IOCTL_PANFROST_SET_LABEL_BO failed (err=%d)", errno);
}

static uint32_t
panthor_kmod_perf_hw_sample_size(struct pan_kmod_dev *dev)
{
   uint32_t hw_blk_cnt =
      2 + pan_query_core_count(&dev->props) + pan_query_l2_slices(&dev->props);
   uint32_t counters_per_block = pan_query_perf_counter_per_block(&dev->props);
   uint32_t hw_blk_sz = counters_per_block * sizeof(uint32_t);

   return hw_blk_cnt * hw_blk_sz;
}

static int
panfrost_kmod_perf_init_sample_layout(struct panfrost_kmod_perf_session *session)
{
   struct pan_kmod_dev *dev = session->base.dev;
   uint32_t hw_blk_cnt =
      2 + pan_query_core_count(&dev->props) + pan_query_l2_slices(&dev->props);
   uint32_t hw_sample_offset = 0;
   int ret;

   ret = pan_kmod_perf_sample_layout_init(dev, &session->sample_layout, hw_blk_cnt);
   if (ret)
      return ret;

   pan_kmod_perf_sample_layout_add_section(dev, &session->sample_layout,
                                           MALI_PERF_BLOCK_GPU_FRONT_END, 0,
                                           &hw_sample_offset);
   pan_kmod_perf_sample_layout_add_section(dev, &session->sample_layout,
                                           MALI_PERF_BLOCK_TILER, 0,
                                           &hw_sample_offset);
   pan_kmod_perf_sample_layout_add_memsys_sections(dev, &session->sample_layout,
                                                   &hw_sample_offset);
   pan_kmod_perf_sample_layout_add_shader_core_sections(
      dev, &session->sample_layout, &hw_sample_offset);
   return 0;
}

static void
panfrost_kmod_perf_destroy(struct pan_kmod_perf_session *session)
{
   struct panfrost_kmod_perf_session *panfrost_session =
      container_of(session, struct panfrost_kmod_perf_session, base);
   struct pan_kmod_dev *dev = session->dev;

   if (p_atomic_read(&panfrost_session->state) != PANFROST_KMOD_PERF_SESSION_STOPPED) {
      p_atomic_set(&panfrost_session->state, PANFROST_KMOD_PERF_SESSION_STOPPING);
      eventfd_write(panfrost_session->eventfd, 1);
      thrd_join(panfrost_session->thread, NULL);
   }

   if (panfrost_session->hw_sample)
      pan_kmod_dev_free(dev, panfrost_session->hw_sample);

   pan_kmod_perf_consolidated_sample_cleanup(
      dev, &panfrost_session->consolidated_sample);
   pan_kmod_perf_dumped_sample_cleanup(dev, &panfrost_session->dumped_sample);
   pan_kmod_perf_sample_layout_cleanup(dev, &panfrost_session->sample_layout);
   pthread_mutex_destroy(&panfrost_session->lock);
   pan_kmod_dev_free(dev, panfrost_session);
}

static void
panfrost_kmod_patch_hw_sample_timestamp(
   void *hw_sample, uint64_t new_ts,
   const struct pan_kmod_perf_sample_layout *layout)
{
   for (uint32_t s = 0; s < layout->section_count; s++) {
      const struct pan_kmod_perf_sample_section *section = &layout->sections[s];
      uint32_t *in = hw_sample + section->hw_sample_offset;
      in[0] = (uint32_t)new_ts;
      in[1] = (uint32_t)(new_ts >> 32);
   }
}

static int
panfrost_kmod_perf_sample_locked(struct panfrost_kmod_perf_session *session)
{
   struct pan_kmod_dev *dev = session->base.dev;
   void *hw_sample = session->hw_sample;
   struct drm_panfrost_perfcnt_dump req = {
      .buf_ptr = (uint64_t)(uintptr_t)hw_sample,
   };

   int ret = pan_kmod_ioctl(dev->fd, DRM_IOCTL_PANFROST_PERFCNT_DUMP, &req);
   if (ret)
      return ret;

   /* XXX: Don't do this once the kernel issue is resolved. */
   uint64_t gpu_ts = pan_kmod_query_timestamp(dev);
   panfrost_kmod_patch_hw_sample_timestamp(hw_sample, gpu_ts,
                                           &session->sample_layout);

   pan_kmod_perf_consolidate_sample(dev, &session->sample_layout, hw_sample,
                                    &session->consolidated_sample);

   return 0;
}

static int
panfrost_kmod_perf_thread(void *data)
{
   struct panfrost_kmod_perf_session *session = data;
   const struct sched_param rt_params = {
      /* Do not use max priority to avoid starving migration and watchdog
       * threads.
       */
      .sched_priority = sched_get_priority_max(SCHED_FIFO) - 1,
   };
   /* This is best effort. If we can't make the thread RT because we don't
    * have CAP_SYS_NICE, we keep going.
    */
   int ret = sched_setscheduler(0, SCHED_FIFO, &rt_params);
   if (ret)
      mesa_logw("Can't make the perfcnt thread RT (err=%d)", errno);

   p_atomic_set(&session->state, PANFROST_KMOD_PERF_SESSION_STARTED);

   while (true) {
      struct pollfd fds[] = {
         { session->eventfd, POLLIN, },
         { session->timerfd, POLLIN, },
      };

      ret = poll(fds, ARRAY_SIZE(fds), INT_MAX);
      if (ret < 0)
         goto err;

      if (fds[0].revents & POLLIN) {
         eventfd_t evt;

         ret = eventfd_read(session->eventfd, &evt);
         if (ret)
            goto err;
      }

      if (fds[1].revents & POLLIN) {
         uint64_t expired = 0;

         read(session->timerfd, &expired, sizeof(expired));
         pthread_mutex_lock(&session->lock);
         panfrost_kmod_perf_sample_locked(session);
         pthread_mutex_unlock(&session->lock);
      }

      if (p_atomic_cmpxchg(&session->state, PANFROST_KMOD_PERF_SESSION_STOPPING,
                           PANFROST_KMOD_PERF_SESSION_STOPPED) ==
          PANFROST_KMOD_PERF_SESSION_STOPPING) {
         return 0;
      }
   }

   return 0;

err:
   p_atomic_set(&session->state, PANFROST_KMOD_PERF_SESSION_STOPPED);
   return ret;
}

static int64_t
panfrost_kmod_perf_get_hw_counter_value(struct mali_perf_backend *backend,
                                       struct mali_perf_hw_counter_id id)
{
   struct panfrost_kmod_perf_session *session = container_of(
      backend, struct panfrost_kmod_perf_session, base.mali_perf_backend);
   uint32_t counters_per_block =
      pan_query_perf_counter_per_block(&session->base.dev->props);
   int64_t *base = session->dumped_sample.data +
                   session->sample_layout.block_type_offset[id.block.type];

   base += counters_per_block * id.block.index;
   return base[id.index];
}

static struct pan_kmod_perf_session *
panfrost_kmod_perf_create(struct pan_kmod_dev *dev)
{
   struct pan_kmod_perf_caps caps = {
      /* FIXME: check if we can make the thread RT before advertising 10KHz
       * sampling.
       */
      .min_sampling_period_ns = 1000000,
   };
   struct mali_perf_backend backend = {
      .get_hw_counter_value = panfrost_kmod_perf_get_hw_counter_value,
   };
   struct panfrost_kmod_perf_session *session;

   session = pan_kmod_dev_alloc(dev, sizeof(*session));
   if (!session)
      return NULL;

   session->eventfd = -1;
   session->timerfd = -1;
   pthread_mutex_init(&session->lock, NULL);
   pan_kmod_perf_session_init(&session->base, dev, caps, backend);

   int ret = panfrost_kmod_perf_init_sample_layout(session);
   if (ret)
      goto err_cleanup_session;

   session->hw_sample =
      pan_kmod_dev_alloc(dev, panthor_kmod_perf_hw_sample_size(dev));
   ret = pan_kmod_perf_consolidated_sample_init(dev, &session->sample_layout,
                                                &session->consolidated_sample);
   ret |= pan_kmod_perf_dumped_sample_init(dev, &session->sample_layout,
                                           &session->dumped_sample);
   if (!session->hw_sample || ret) {
      mesa_loge("failed to allocate the consolidated sample buffer");
      goto err_cleanup_session;
   }

   session->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
   if (session->timerfd < 0) {
      mesa_loge("timerfd_create() failed (err=%d)", errno);
      goto err_cleanup_session;
   }

   session->eventfd = eventfd(0, 0);
   if (session->eventfd < 0) {
      mesa_loge("eventfd() failed (err=%d)", errno);
      goto err_cleanup_session;
   }

   p_atomic_set(&session->state, PANFROST_KMOD_PERF_SESSION_STARTING);
   ret = u_thread_create(&session->thread, panfrost_kmod_perf_thread, session);
   if (ret) {
      p_atomic_set(&session->state, PANFROST_KMOD_PERF_SESSION_STOPPED);
      goto err_cleanup_session;
   }

   return &session->base;

err_cleanup_session:
   panfrost_kmod_perf_destroy(&session->base);
   return NULL;
}

static int
panfrost_kmod_perf_enable(struct pan_kmod_perf_session *session,
                          const struct pan_kmod_perf_config *cfg)
{
   struct panfrost_kmod_perf_session *panfrost_session =
      container_of(session, struct panfrost_kmod_perf_session, base);
   struct drm_panfrost_perfcnt_enable req = {
      .enable = 1,
   };
   int ret;

   ret =
      pan_kmod_ioctl(session->dev->fd, DRM_IOCTL_PANFROST_PERFCNT_ENABLE, &req);
   if (ret) {
      mesa_loge("DRM_IOCTL_PANFROST_PERFCNT_ENABLE failed (err=%d)", errno);
      if (errno == ENOSYS)
         mesa_loge("try `# echo Y > /sys/module/panfrost/parameters/unstable_ioctls\n`");
      return ret;
   }

   struct itimerspec ts = {
      .it_interval = {
         .tv_nsec = cfg->sampling_period_ns % 1000000000ull,
         .tv_sec = cfg->sampling_period_ns / 1000000000ull,
      },
   };

   ts.it_value = ts.it_interval;

   ret = timerfd_settime(panfrost_session->timerfd, 0, &ts, NULL);
   if (ret)
      return ret;

   session->config = *cfg;

   /* Counters start accumulating once they are enabled. */
   uint64_t gpu_ts = pan_kmod_query_timestamp(session->dev);
   pthread_mutex_lock(&panfrost_session->lock);
   panfrost_session->consolidated_sample.time_span.start_gpu_ts = gpu_ts;
   panfrost_session->consolidated_sample.time_span.end_gpu_ts = gpu_ts;
   pthread_mutex_unlock(&panfrost_session->lock);

   return 0;
}

static int
panfrost_kmod_perf_disable(struct pan_kmod_perf_session *session)
{
   struct panfrost_kmod_perf_session *panfrost_session =
      container_of(session, struct panfrost_kmod_perf_session, base);
   struct drm_panfrost_perfcnt_enable req = {
      .enable = 0,
   };
   struct itimerspec ts = {0};

   timerfd_settime(panfrost_session->timerfd, 0, &ts, NULL);
   return pan_kmod_ioctl(session->dev->fd, DRM_IOCTL_PANFROST_PERFCNT_ENABLE,
                         &req);
}

static void
panfrost_kmod_perf_dump(struct pan_kmod_perf_session *session,
                        struct mali_perf_dump_info *info)
{
   struct panfrost_kmod_perf_session *panfrost_session =
      container_of(session, struct panfrost_kmod_perf_session, base);
   struct pan_kmod_dev *dev = session->dev;

   pthread_mutex_lock(&panfrost_session->lock);
   *info = pan_kmod_perf_dump_sample(dev, &panfrost_session->sample_layout,
                                     &panfrost_session->consolidated_sample,
                                     &panfrost_session->dumped_sample);
   pthread_mutex_unlock(&panfrost_session->lock);
}

const struct pan_kmod_ops panfrost_kmod_ops = {
   .dev_create = panfrost_kmod_dev_create,
   .dev_destroy = panfrost_kmod_dev_destroy,
   .dev_query_user_va_range = panfrost_kmod_dev_query_user_va_range,
   .bo_alloc = panfrost_kmod_bo_alloc,
   .bo_free = panfrost_kmod_bo_free,
   .bo_import = panfrost_kmod_bo_import,
   .bo_get_mmap_offset = panfrost_kmod_bo_get_mmap_offset,
   .bo_wait = panfrost_kmod_bo_wait,
   .flush_bo_map_syncs = panfrost_kmod_flush_bo_map_syncs,
   .bo_make_evictable = panfrost_kmod_bo_make_evictable,
   .bo_make_unevictable = panfrost_kmod_bo_make_unevictable,
   .vm_create = panfrost_kmod_vm_create,
   .vm_destroy = panfrost_kmod_vm_destroy,
   .vm_bind = panfrost_kmod_vm_bind,
   .query_timestamp = panfrost_kmod_query_timestamp,
   .bo_set_label = panfrost_kmod_bo_label,
   .perf_create = panfrost_kmod_perf_create,
   .perf_destroy = panfrost_kmod_perf_destroy,
   .perf_enable = panfrost_kmod_perf_enable,
   .perf_disable = panfrost_kmod_perf_disable,
   .perf_dump = panfrost_kmod_perf_dump,
};
