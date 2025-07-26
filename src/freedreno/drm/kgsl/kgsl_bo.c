#include "kgsl_priv.h"
#include "util/os_file.h"
#include "util/os_mman.h"

#include <linux/dma-heap.h>

static uint64_t
kgsl_bo_iova(struct fd_bo *bo)
{
    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    return kgsl_bo->iova;
}

static void
kgsl_bo_set_name(struct fd_bo *bo, const char *fmt, va_list ap)
{
    /* This function is a no op for KGSL */
    return;
}

static int
kgsl_bo_offset(struct fd_bo *bo, uint64_t *offset)
{
    /* from tu_kgsl.c - offset for mmap needs to be */
    /* the returned alloc id shifted over 12 */
    *offset = bo->handle << 12;
    return 0;
}

static int
kgsl_bo_madvise(struct fd_bo *bo, int willneed)
{
    /* KGSL does not support this, so simply return willneed */
    return willneed;
}

static int
kgsl_bo_cpu_prep(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t op)
{
    /* only need to handle implicit sync here which is a NOP for KGSL */
    return 0;
}

void
kgsl_bo_close_handle(struct fd_bo *bo)
{
    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    if (kgsl_bo->bo_type == KGSL_BO_IMPORT) {
       close(kgsl_bo->import_fd);
    }

    struct kgsl_gpumem_free_id req = {
        .id = bo->handle
    };

    kgsl_pipe_safe_ioctl(bo->dev->fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
}

static void
kgsl_bo_destroy(struct fd_bo *bo)
{
    /* KGSL will immediately delete the BO when we close
     * the handle, so wait on all fences to ensure
     * the GPU is done using it before we destory it
     */
    for (uint32_t i = 0; i < bo->nr_fences; i++) {
        struct fd_pipe *pipe = bo->fences[i]->pipe;
        pipe->funcs->wait(pipe, bo->fences[i], ~0);
    }

    fd_bo_fini_common(bo);
}

static void *
kgsl_bo_map(struct fd_bo *bo)
{
    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    if (!bo->map) {
        if (kgsl_bo->bo_type == KGSL_BO_IMPORT) {
            /* in `fd_bo_map` if it tries to mmap this BO. mmap logic is copied from
             * https://android.googlesource.com/platform/hardware/libhardware/+/master/modules/gralloc/mapper.cpp#44
             */
            bo->map = os_mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, kgsl_bo->import_fd, 0);
        } else {
            uint64_t offset;
            int ret;

            ret = bo->funcs->offset(bo, &offset);
            if (ret) {
                return NULL;
            }

            bo->map = os_mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    bo->dev->fd, offset);
            if (bo->map == MAP_FAILED) {
                ERROR_MSG("mmap failed: %s", strerror(errno));
                bo->map = NULL;
            }
        }

        if (bo->map == MAP_FAILED) {
            ERROR_MSG("mmap failed: %s", strerror(errno));
            bo->map = NULL;
        }
    }
    return bo->map;
}

static int kgsl_bo_dmabuf(struct fd_bo *bo) {
    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    return os_dupfd_cloexec(kgsl_bo->import_fd);
}

static const struct fd_bo_funcs bo_funcs = {
    .iova = kgsl_bo_iova,
    .set_name = kgsl_bo_set_name,
    .offset = kgsl_bo_offset,
    .map = kgsl_bo_map,
    .madvise = kgsl_bo_madvise,
    .cpu_prep = kgsl_bo_cpu_prep,
    .destroy = kgsl_bo_destroy,
    .dmabuf = kgsl_bo_dmabuf,
};

/* Size is not used by KGSL */
struct fd_bo *
kgsl_bo_from_handle(struct fd_device *dev, uint32_t size, uint32_t handle)
{
    struct fd_bo *bo;
    int ret;
    struct kgsl_gpuobj_info req = {
        .id = handle,
    };

    ret = kgsl_pipe_safe_ioctl(dev->fd,
            IOCTL_KGSL_GPUOBJ_INFO, &req);

    if (ret) {
        ERROR_MSG("Failed to get handle info (%s)", strerror(errno));
        return NULL;
    }

    struct kgsl_bo *kgsl_bo = calloc(1, sizeof(*kgsl_bo));
    if (!kgsl_bo)
        return NULL;

    bo = &kgsl_bo->base;
    bo->dev = dev;
    bo->size = req.size;
    bo->handle = req.id;
    bo->funcs = &bo_funcs;

    kgsl_bo->iova = req.gpuaddr;

    fd_bo_init_common(bo, dev);

    return bo;
}

struct fd_bo *
kgsl_bo_from_dmabuf(struct fd_device *dev, int fd)
{
    struct fd_bo *bo;
    struct kgsl_gpuobj_import_dma_buf import_dmabuf = {
        .fd = fd,
    };
    struct kgsl_gpuobj_import req = {
        .priv = (uintptr_t)&import_dmabuf,
        .priv_len = sizeof(import_dmabuf),
        .flags = 0,
        .type = KGSL_USER_MEM_TYPE_DMABUF,
    };
    int ret;

    ret = kgsl_pipe_safe_ioctl(dev->fd,
            IOCTL_KGSL_GPUOBJ_IMPORT, &req);

    if (ret) {
        ERROR_MSG("Failed to import dma-buf (%s)", strerror(errno));
        return NULL;
    }

    bo = fd_bo_from_handle(dev, req.id, 0);

    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    kgsl_bo->bo_type = KGSL_BO_IMPORT;
    kgsl_bo->import_fd = os_dupfd_cloexec(fd);

    return bo;
}

static int
dma_heap_alloc(uint64_t size)
{
   int ret;
   int dma_heap = open("/dev/dma_heap/system", O_RDONLY);

   if (dma_heap < 0) {
      int ion_heap = open("/dev/ion", O_RDONLY);

      if (ion_heap < 0)
         return -1;

      struct ion_allocation_data {
         __u64 len;
         __u32 heap_id_mask;
         __u32 flags;
         __u32 fd;
         __u32 unused;
      } alloc_data = {
         .len = size,
         /* ION_HEAP_SYSTEM | ION_SYSTEM_HEAP_ID */
         .heap_id_mask = (1U << 0) | (1U << 25),
         .flags = 0, /* uncached */
      };

      ret = kgsl_pipe_safe_ioctl(ion_heap, _IOWR('I', 0, struct ion_allocation_data),
                      &alloc_data);

      close(ion_heap);

      if (ret)
         return -1;

      return alloc_data.fd;
   } else {
      struct dma_heap_allocation_data alloc_data = {
         .len = size,
         .fd_flags = O_RDWR | O_CLOEXEC,
      };

      ret = kgsl_pipe_safe_ioctl(dma_heap, DMA_HEAP_IOCTL_ALLOC, &alloc_data);

      close(dma_heap);

      if (ret)
         return -1;

      return alloc_data.fd;
   }
}

static struct fd_bo *
kgsl_bo_new_dmabuf(struct fd_device *dev, uint32_t size)
{
   int fd;
   struct fd_bo *bo;

   fd = dma_heap_alloc(size);
   if (fd < 0) {
      ERROR_MSG("Failed to allocate dma-buf (%s)", strerror(errno));
      return NULL;
   }

   bo = kgsl_bo_from_dmabuf(dev, fd);

   close(fd);
   return bo;
}

struct fd_bo *
kgsl_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags)
{
    if (flags & (FD_BO_SHARED | FD_BO_SCANOUT)) {
       return kgsl_bo_new_dmabuf(dev, size);
    }

    int ret;
    struct fd_bo *bo;
    struct kgsl_gpumem_alloc_id req = {
        .size = size,
    };

    if (flags & FD_BO_GPUREADONLY)
        req.flags |= KGSL_MEMFLAGS_GPUREADONLY;

    ret = kgsl_pipe_safe_ioctl(dev->fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &req);

    if (ret) {
        ERROR_MSG("GPUMEM_ALLOC_ID failed (%s)", strerror(errno));
        return NULL;
    }

    bo = kgsl_bo_from_handle(dev, size, req.id);

    struct kgsl_bo *kgsl_bo = to_kgsl_bo(bo);
    kgsl_bo->bo_type = KGSL_BO_NATIVE;

    if (!bo) {
        ERROR_MSG("Failed to allocate buffer object");
        return NULL;
    }

    return bo;
}
