/*
 * Copyright © 2022 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "virtio_priv.h"

static int
bo_allocate(struct virtio_bo *virtio_bo)
{
   struct fd_bo *bo = &virtio_bo->base;
   if (!virtio_bo->offset) {
      struct drm_virtgpu_map req = {
         .handle = bo->handle,
      };
      int ret;

      ret = drmIoctl(bo->dev->fd, DRM_IOCTL_VIRTGPU_MAP, &req);
      if (ret) {
         ERROR_MSG("alloc failed: %s", strerror(errno));
         return ret;
      }

      virtio_bo->offset = req.offset;
   }

   return 0;
}

static int
virtio_bo_offset(struct fd_bo *bo, uint64_t *offset)
{
   struct virtio_bo *virtio_bo = to_virtio_bo(bo);
   int ret = bo_allocate(virtio_bo);
   if (ret)
      return ret;
   *offset = virtio_bo->offset;
   return 0;
}

static int
virtio_bo_cpu_prep_guest(struct fd_bo *bo)
{
   struct drm_virtgpu_3d_wait args = {
         .handle = bo->handle,
   };
   int ret;

   /* Side note, this ioctl is defined as IO_WR but should be IO_W: */
   ret = drmIoctl(bo->dev->fd, DRM_IOCTL_VIRTGPU_WAIT, &args);
   if (ret && errno == EBUSY)
      return -EBUSY;

   return 0;
}

static int
virtio_bo_cpu_prep(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t op)
{
   int ret;

   /*
    * Wait first in the guest, to avoid a blocking call in host.
    * If implicit sync it used, we still need to *also* wait in
    * host, if it is a shared buffer, because the guest doesn't
    * know about usage of the bo in the host (or other guests).
    */

   ret = virtio_bo_cpu_prep_guest(bo);
   if (ret)
      goto out;

   /* If buffer is not shared, then it is not shared with host,
    * so we don't need to worry about implicit sync in host:
    */
   if (!bo->shared)
      goto out;

   /* If buffer is shared, but we are using explicit sync, no
    * need to fallback to implicit sync in host:
    */
   if (pipe && to_virtio_pipe(pipe)->no_implicit_sync)
      goto out;

   struct msm_ccmd_gem_cpu_prep_req req = {
         .hdr = MSM_CCMD(GEM_CPU_PREP, sizeof(req)),
         .host_handle = to_virtio_bo(bo)->host_handle,
         .op = op,
         .timeout = 5000000000,
   };
   struct msm_ccmd_gem_cpu_prep_rsp *rsp;

   rsp = virtio_alloc_rsp(bo->dev, &req.hdr, sizeof(*rsp));

   ret = virtio_execbuf(bo->dev, &req.hdr, true);
   if (ret)
      goto out;

   ret = rsp->ret;

out:
   return ret;
}

static void
virtio_bo_cpu_fini(struct fd_bo *bo)
{
   /* no-op */
}

static int
virtio_bo_madvise(struct fd_bo *bo, int willneed)
{
   /* TODO:
    * Currently unsupported, synchronous WILLNEED calls would introduce too
    * much latency.. ideally we'd keep state in the guest and only flush
    * down to host when host is under memory pressure.  (Perhaps virtio-balloon
    * could signal this?)
    */
   return willneed;
}

static uint64_t
virtio_bo_iova(struct fd_bo *bo)
{
   /* The shmem bo is allowed to have no iova, as it is only used for
    * guest<->host communications:
    */
   assert(bo->iova || (to_virtio_bo(bo)->blob_id == 0));
   return bo->iova;
}

static void
virtio_bo_set_name(struct fd_bo *bo, const char *fmt, va_list ap)
{
   char name[32];
   int sz;

   /* Note, we cannot set name on the host for the shmem bo, as
    * that isn't a real gem obj on the host side.. not having
    * an iova is a convenient way to detect this case:
    */
   if (!bo->iova)
      return;

   sz = vsnprintf(name, sizeof(name), fmt, ap);
   sz = MIN2(sz, sizeof(name));

   unsigned req_len = sizeof(struct msm_ccmd_gem_set_name_req) + align(sz, 4);

   uint8_t buf[req_len];
   struct msm_ccmd_gem_set_name_req *req = (void *)buf;

   req->hdr = MSM_CCMD(GEM_SET_NAME, req_len);
   req->host_handle = to_virtio_bo(bo)->host_handle;
   req->len = sz;

   memcpy(req->payload, name, sz);

   virtio_execbuf(bo->dev, &req->hdr, false);
}

static void
virtio_bo_upload(struct fd_bo *bo, void *src, unsigned len)
{
   unsigned req_len = sizeof(struct msm_ccmd_gem_upload_req) + align(len, 4);

   uint8_t buf[req_len];
   struct msm_ccmd_gem_upload_req *req = (void *)buf;

   req->hdr = MSM_CCMD(GEM_UPLOAD, req_len);
   req->host_handle = to_virtio_bo(bo)->host_handle;
   req->pad = 0;
   req->off = 0;
   req->len = len;

   memcpy(req->payload, src, len);

   virtio_execbuf(bo->dev, &req->hdr, false);
}

static void
virtio_bo_destroy(struct fd_bo *bo)
{
   struct virtio_bo *virtio_bo = to_virtio_bo(bo);
   free(virtio_bo);
}

static const struct fd_bo_funcs funcs = {
   .offset = virtio_bo_offset,
   .cpu_prep = virtio_bo_cpu_prep,
   .cpu_fini = virtio_bo_cpu_fini,
   .madvise = virtio_bo_madvise,
   .iova = virtio_bo_iova,
   .set_name = virtio_bo_set_name,
   .upload = virtio_bo_upload,
   .destroy = virtio_bo_destroy,
};

static struct fd_bo *
bo_from_handle(struct fd_device *dev, uint32_t size, uint32_t handle)
{
   struct virtio_bo *virtio_bo;
   struct fd_bo *bo;

   virtio_bo = calloc(1, sizeof(*virtio_bo));
   if (!virtio_bo)
      return NULL;

   bo = &virtio_bo->base;
   bo->size = size;
   bo->funcs = &funcs;
   bo->handle = handle;

   return bo;
}

/* allocate a new buffer object from existing handle */
struct fd_bo *
virtio_bo_from_handle(struct fd_device *dev, uint32_t size, uint32_t handle)
{
   struct fd_bo *bo = bo_from_handle(dev, size, handle);
   struct drm_virtgpu_resource_info args = {
         .bo_handle = handle,
   };
   int ret;

   ret = drmCommandWriteRead(dev->fd, DRM_VIRTGPU_RESOURCE_INFO, &args, sizeof(args));
   if (ret) {
      INFO_MSG("failed to get resource info: %s", strerror(errno));
      goto fail;
   }

   struct msm_ccmd_gem_info_req req = {
         .hdr = MSM_CCMD(GEM_INFO, sizeof(req)),
         .res_id = args.res_handle,
         .blob_mem = args.blob_mem,
         .blob_id = p_atomic_inc_return(&to_virtio_device(dev)->next_blob_id),
   };

   struct msm_ccmd_gem_info_rsp *rsp =
         virtio_alloc_rsp(dev, &req.hdr, sizeof(*rsp));

   ret = virtio_execbuf(dev, &req.hdr, true);
   if (ret) {
      INFO_MSG("failed to get gem info: %s", strerror(errno));
      goto fail;
   }
   if (rsp->ret) {
      INFO_MSG("failed (on host) to get gem info: %s", strerror(rsp->ret));
      goto fail;
   }

   struct virtio_bo *virtio_bo = to_virtio_bo(bo);

   virtio_bo->blob_id = req.blob_id;
   virtio_bo->host_handle = rsp->host_handle;
   bo->iova = rsp->iova;

   /* If the imported buffer is allocated via virgl context (for example
    * minigbm/arc-cros-gralloc) then the guest gem object size is fake,
    * potentially not accounting for UBWC meta data, required pitch
    * alignment, etc.  But in the import path the gallium driver checks
    * that the size matches the minimum size based on layout.  So replace
    * the guest potentially-fake size with the real size from the host:
    */
   bo->size = rsp->size;

   return bo;

fail:
   virtio_bo_destroy(bo);
   return NULL;
}

/* allocate a buffer object: */
struct fd_bo *
virtio_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);
   struct drm_virtgpu_resource_create_blob args = {
         .blob_mem   = VIRTGPU_BLOB_MEM_HOST3D,
         .size       = size,
   };
   struct msm_ccmd_gem_new_req req = {
         .hdr = MSM_CCMD(GEM_NEW, sizeof(req)),
         .size = size,
   };
   struct msm_ccmd_gem_new_rsp *rsp = NULL;
   int ret;

   if (flags & FD_BO_SCANOUT)
      req.flags |= MSM_BO_SCANOUT;

   if (flags & FD_BO_GPUREADONLY)
      req.flags |= MSM_BO_GPU_READONLY;

   if (flags & FD_BO_CACHED_COHERENT) {
      req.flags |= MSM_BO_CACHED_COHERENT;
   } else {
      req.flags |= MSM_BO_WC;
   }

   if (flags & _FD_BO_VIRTIO_SHM) {
      args.blob_id = 0;
      args.blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
   } else {
      if (flags & (FD_BO_SHARED | FD_BO_SCANOUT)) {
         args.blob_flags = VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE |
               VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
      } else if (!(flags & FD_BO_NOMAP)) {
         args.blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
      }
      args.blob_id = p_atomic_inc_return(&virtio_dev->next_blob_id);
      args.cmd = VOID2U64(&req);
      args.cmd_size = sizeof(req);

      /* tunneled cmds are processed separately on host side,
       * before the renderer->get_blob() callback.. the blob_id
       * is used to like the created bo to the get_blob() call
       */
      req.blob_id = args.blob_id;

      rsp = virtio_alloc_rsp(dev, &req.hdr, sizeof(*rsp));
   }

   simple_mtx_lock(&virtio_dev->eb_lock);
   if (rsp)
      req.hdr.seqno = ++virtio_dev->next_seqno;
   ret = drmIoctl(dev->fd, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &args);
   simple_mtx_unlock(&virtio_dev->eb_lock);
   if (ret)
      goto fail;

   struct fd_bo *bo = bo_from_handle(dev, size, args.bo_handle);
   struct virtio_bo *virtio_bo = to_virtio_bo(bo);

   virtio_bo->blob_id = args.blob_id;

   if (rsp) {
      /* RESOURCE_CREATE_BLOB is async, so we need to wait for host..
       * which is a bit unfortunate, but better to sync here than
       * add extra code to check if we need to wait each time we
       * emit a reloc.
       */
      virtio_host_sync(dev, &req.hdr);

      virtio_bo->host_handle = rsp->host_handle;
      bo->iova = rsp->iova;
   }

   return bo;

fail:
   return NULL;
}
