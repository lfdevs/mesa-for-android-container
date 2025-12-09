#include "nouveau_bo.h"

#include "drm-uapi/nouveau_drm.h"
#include "util/hash_table.h"
#include "util/u_math.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "nvidia/classes/cl9097.h"
#include "nvidia/classes/clc597.h"

void
nouveau_ws_bo_unbind_vma(struct nouveau_ws_device *dev,
                         uint64_t offset, uint64_t range)
{
   assert(dev->has_vm_bind);

   struct drm_nouveau_vm_bind_op newbindop = {
      .op = DRM_NOUVEAU_VM_BIND_OP_UNMAP,
      .addr = offset,
      .range = range,
   };
   struct drm_nouveau_vm_bind vmbind = {
      .op_count = 1,
      .op_ptr = (uint64_t)(uintptr_t)(void *)&newbindop,
   };
   ASSERTED int ret = drmCommandWriteRead(dev->fd, DRM_NOUVEAU_VM_BIND,
                                          &vmbind, sizeof(vmbind));
   assert(ret == 0);
}

void
nouveau_ws_bo_bind_vma(struct nouveau_ws_device *dev,
                       struct nouveau_ws_bo *bo,
                       uint64_t addr,
                       uint64_t range,
                       uint64_t bo_offset,
                       uint32_t pte_kind)
{
   assert(dev->has_vm_bind);

   struct drm_nouveau_vm_bind_op newbindop = {
      .op = DRM_NOUVEAU_VM_BIND_OP_MAP,
      .handle = bo->handle,
      .addr = addr,
      .range = range,
      .bo_offset = bo_offset,
      .flags = pte_kind,
   };
   struct drm_nouveau_vm_bind vmbind = {
      .op_count = 1,
      .op_ptr = (uint64_t)(uintptr_t)(void *)&newbindop,
   };
   ASSERTED int ret = drmCommandWriteRead(dev->fd, DRM_NOUVEAU_VM_BIND,
                                          &vmbind, sizeof(vmbind));
   assert(ret == 0);
}

struct nouveau_ws_bo *
nouveau_ws_bo_new_mapped(struct nouveau_ws_device *dev,
                         uint64_t size, uint64_t align,
                         enum nouveau_ws_bo_flags flags,
                         enum nouveau_ws_bo_map_flags map_flags,
                         void **map_out)
{
   struct nouveau_ws_bo *bo = nouveau_ws_bo_new(dev, size, align,
                                                flags | NOUVEAU_WS_BO_MAP);
   if (!bo)
      return NULL;

   void *map = nouveau_ws_bo_map(bo, map_flags);
   if (map == NULL) {
      nouveau_ws_bo_destroy(bo);
      return NULL;
   }

   *map_out = map;
   return bo;
}

static struct nouveau_ws_bo *
nouveau_ws_bo_new_tiled_locked(struct nouveau_ws_device *dev,
                               uint64_t size, uint64_t align,
                               uint8_t pte_kind, uint16_t tile_mode,
                               enum nouveau_ws_bo_flags flags)
{
   struct drm_nouveau_gem_new req = {};

   /* if the caller doesn't care, use the GPU page size */
   if (align == 0)
      align = 0x1000;

   /* Align the size */
   size = align64(size, align);

   req.info.domain = 0;

   /* It needs to live somewhere */
   assert((flags & NOUVEAU_WS_BO_VRAM) || (flags & NOUVEAU_WS_BO_GART));

   if (flags & NOUVEAU_WS_BO_VRAM)
      req.info.domain |= NOUVEAU_GEM_DOMAIN_VRAM;

   if (flags & NOUVEAU_WS_BO_GART)
      req.info.domain |= NOUVEAU_GEM_DOMAIN_GART;

   if (flags & NOUVEAU_WS_BO_MAP)
      req.info.domain |= NOUVEAU_GEM_DOMAIN_MAPPABLE;

   if (flags & NOUVEAU_WS_BO_NO_SHARE)
      req.info.domain |= NOUVEAU_GEM_DOMAIN_NO_SHARE;

   if (flags & NOUVEAU_WS_BO_COHERENT)
      req.info.domain |= NOUVEAU_GEM_DOMAIN_COHERENT;

   req.info.tile_flags = (uint32_t)pte_kind << 8;
   req.info.tile_mode = tile_mode;

   req.info.size = size;
   req.align = align;

   int ret = drmCommandWriteRead(dev->fd, DRM_NOUVEAU_GEM_NEW, &req, sizeof(req));
   if (ret != 0)
      return NULL;

   struct nouveau_ws_bo *bo = CALLOC_STRUCT(nouveau_ws_bo);
   bo->size = size;
   bo->handle = req.info.handle;
   bo->map_handle = req.info.map_handle;
   bo->dev = dev;
   bo->flags = flags;
   bo->refcnt = 1;
   bo->pte_kind = pte_kind;

   _mesa_hash_table_insert(dev->bos, (void *)(uintptr_t)bo->handle, bo);

   return bo;
}

struct nouveau_ws_bo *
nouveau_ws_bo_new_tiled(struct nouveau_ws_device *dev,
                        uint64_t size, uint64_t align,
                        uint8_t pte_kind, uint16_t tile_mode,
                        enum nouveau_ws_bo_flags flags)
{
   struct nouveau_ws_bo *bo;

   simple_mtx_lock(&dev->bos_lock);
   bo = nouveau_ws_bo_new_tiled_locked(dev, size, align,
                                       pte_kind, tile_mode, flags);
   simple_mtx_unlock(&dev->bos_lock);

   return bo;
}

struct nouveau_ws_bo *
nouveau_ws_bo_new(struct nouveau_ws_device *dev,
                  uint64_t size, uint64_t align,
                  enum nouveau_ws_bo_flags flags)
{
   return nouveau_ws_bo_new_tiled(dev, size, align, 0, 0, flags);
}

static struct nouveau_ws_bo *
nouveau_ws_bo_from_dma_buf_locked(struct nouveau_ws_device *dev, int fd)
{
   uint32_t handle;
   int ret = drmPrimeFDToHandle(dev->fd, fd, &handle);
   if (ret != 0)
      return NULL;

   struct hash_entry *entry =
      _mesa_hash_table_search(dev->bos, (void *)(uintptr_t)handle);
   if (entry != NULL) {
      struct nouveau_ws_bo *bo = entry->data;
      nouveau_ws_bo_ref(bo);
      return bo;
   }

   /*
    * If we got here, no BO exists for the retrieved handle. If we error
    * after this point, we need to close the handle.
    */

   struct drm_nouveau_gem_info info = {
      .handle = handle
   };
   ret = drmCommandWriteRead(dev->fd, DRM_NOUVEAU_GEM_INFO,
                             &info, sizeof(info));
   if (ret != 0)
      goto fail_fd_to_handle;

   enum nouveau_ws_bo_flags flags = 0;
   if (info.domain & NOUVEAU_GEM_DOMAIN_VRAM)
      flags |= NOUVEAU_WS_BO_VRAM;
   if (info.domain & NOUVEAU_GEM_DOMAIN_GART)
      flags |= NOUVEAU_WS_BO_GART;
   if (info.domain & NOUVEAU_GEM_DOMAIN_COHERENT)
      flags |= NOUVEAU_WS_BO_COHERENT;
   if (info.map_handle)
      flags |= NOUVEAU_WS_BO_MAP;

   struct nouveau_ws_bo *bo = CALLOC_STRUCT(nouveau_ws_bo);
   bo->size = info.size;
   bo->handle = info.handle;
   bo->map_handle = info.map_handle;
   bo->dev = dev;
   bo->flags = flags;
   bo->refcnt = 1;
   bo->pte_kind = (info.tile_flags & 0x0000ff00) >> 8;

   _mesa_hash_table_insert(dev->bos, (void *)(uintptr_t)handle, bo);

   return bo;

fail_fd_to_handle:
   drmCloseBufferHandle(dev->fd, handle);

   return NULL;
}

struct nouveau_ws_bo *
nouveau_ws_bo_from_dma_buf(struct nouveau_ws_device *dev, int fd)
{
   struct nouveau_ws_bo *bo;

   simple_mtx_lock(&dev->bos_lock);
   bo = nouveau_ws_bo_from_dma_buf_locked(dev, fd);
   simple_mtx_unlock(&dev->bos_lock);

   return bo;
}

static bool
atomic_dec_not_one(atomic_uint_fast32_t *counter)
{
   uint_fast32_t old = *counter;
   while (1) {
      assert(old != 0);
      if (old == 1)
         return false;

      if (atomic_compare_exchange_weak(counter, &old, old - 1))
         return true;
   }
}

void
nouveau_ws_bo_destroy(struct nouveau_ws_bo *bo)
{
   if (atomic_dec_not_one(&bo->refcnt))
      return;

   struct nouveau_ws_device *dev = bo->dev;

   /* Lock the device before we drop the final reference */
   simple_mtx_lock(&dev->bos_lock);

   if (--bo->refcnt == 0) {
      _mesa_hash_table_remove_key(dev->bos, (void *)(uintptr_t)bo->handle);

      drmCloseBufferHandle(bo->dev->fd, bo->handle);
      FREE(bo);
   }

   simple_mtx_unlock(&dev->bos_lock);
}

void *
nouveau_ws_bo_map(struct nouveau_ws_bo *bo,
                  enum nouveau_ws_bo_map_flags flags)
{
   int prot = 0;
   if (flags & NOUVEAU_WS_BO_RD)
      prot |= PROT_READ;
   if (flags & NOUVEAU_WS_BO_WR)
      prot |= PROT_WRITE;


   void *res = mmap(NULL, bo->size, prot, MAP_SHARED,
                    bo->dev->fd, bo->map_handle);
   if (res == MAP_FAILED)
      return NULL;

   return res;
}

void
nouveau_ws_bo_unmap(struct nouveau_ws_bo *bo, void *ptr)
{
   munmap(ptr, bo->size);
}

bool
nouveau_ws_bo_wait(struct nouveau_ws_bo *bo, enum nouveau_ws_bo_map_flags flags)
{
   struct drm_nouveau_gem_cpu_prep req = {};

   req.handle = bo->handle;
   if (flags & NOUVEAU_WS_BO_WR)
      req.flags |= NOUVEAU_GEM_CPU_PREP_WRITE;

   return !drmCommandWrite(bo->dev->fd, DRM_NOUVEAU_GEM_CPU_PREP, &req, sizeof(req));
}

int
nouveau_ws_bo_dma_buf(struct nouveau_ws_bo *bo, int *fd)
{
   return drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC | O_RDWR, fd);
}
