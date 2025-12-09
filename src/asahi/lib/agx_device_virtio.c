/*
 * Copyright 2024 Sergio Lopez
 * SPDX-License-Identifier: MIT
 */

#include "agx_device_virtio.h"

#include <inttypes.h>
#include <sys/mman.h>

#include "drm-uapi/virtgpu_drm.h"

#include "vdrm.h"

#include "asahi_proto.h"

/**
 * Helper for simple pass-thru ioctls
 */
int
agx_virtio_simple_ioctl(struct agx_device *dev, unsigned cmd, void *_req)
{
   struct vdrm_device *vdrm = dev->vdrm;
   unsigned req_len = sizeof(struct asahi_ccmd_ioctl_simple_req);
   unsigned rsp_len = sizeof(struct asahi_ccmd_ioctl_simple_rsp);

   req_len += _IOC_SIZE(cmd);
   if (cmd & IOC_OUT)
      rsp_len += _IOC_SIZE(cmd);

   uint8_t buf[req_len];
   struct asahi_ccmd_ioctl_simple_req *req = (void *)buf;
   struct asahi_ccmd_ioctl_simple_rsp *rsp;

   req->hdr = ASAHI_CCMD(IOCTL_SIMPLE, req_len);
   req->cmd = cmd;
   memcpy(req->payload, _req, _IOC_SIZE(cmd));

   rsp = vdrm_alloc_rsp(vdrm, &req->hdr, rsp_len);

   int ret = vdrm_send_req(vdrm, &req->hdr, true);
   if (ret) {
      fprintf(stderr, "simple_ioctl: vdrm_send_req failed\n");
      return ret;
   }

   if (cmd & IOC_OUT)
      memcpy(_req, rsp->payload, _IOC_SIZE(cmd));

   return rsp->ret;
}

static struct agx_bo *
agx_virtio_bo_alloc(struct agx_device *dev, size_t size, size_t align,
                    enum agx_bo_flags flags)
{
   struct agx_bo *bo;
   unsigned handle = 0;

   /* executable implies low va */
   assert(!(flags & AGX_BO_EXEC) || (flags & AGX_BO_LOW_VA));

   struct asahi_ccmd_gem_new_req req = {
      .hdr = ASAHI_CCMD(GEM_NEW, sizeof(req)),
      .size = size,
   };

   if (flags & AGX_BO_WRITEBACK)
      req.flags |= DRM_ASAHI_GEM_WRITEBACK;

   uint32_t blob_flags =
      VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE;

   req.bind_flags = DRM_ASAHI_BIND_READ;
   if (!(flags & AGX_BO_READONLY)) {
      req.bind_flags |= DRM_ASAHI_BIND_WRITE;
   }

   uint32_t blob_id = p_atomic_inc_return(&dev->next_blob_id);

   enum agx_va_flags va_flags = flags & AGX_BO_LOW_VA ? AGX_VA_USC : 0;
   struct agx_va *va = agx_va_alloc(dev, size, align, va_flags, 0);
   if (!va) {
      fprintf(stderr, "Failed to allocate BO VMA\n");
      return NULL;
   }

   req.addr = va->addr;
   req.blob_id = blob_id;
   req.vm_id = dev->vm_id;

   handle = vdrm_bo_create(dev->vdrm, size, blob_flags, blob_id, &req.hdr);
   if (!handle) {
      fprintf(stderr, "vdrm_bo_created failed\n");
      return NULL;
   }

   pthread_mutex_lock(&dev->bo_map_lock);
   bo = agx_lookup_bo(dev, handle);
   dev->max_handle = MAX2(dev->max_handle, handle);
   pthread_mutex_unlock(&dev->bo_map_lock);

   /* Fresh handle */
   assert(!memcmp(bo, &((struct agx_bo){}), sizeof(*bo)));

   bo->dev = dev;
   bo->size = size;
   bo->align = align;
   bo->flags = flags;
   bo->handle = handle;
   bo->prime_fd = -1;
   bo->va = va;
   bo->uapi_handle = vdrm_handle_to_res_id(dev->vdrm, handle);
   return bo;
}

static int
agx_virtio_bo_bind(struct agx_device *dev, struct drm_asahi_gem_bind_op *ops,
                   uint32_t count)
{
   size_t payload_size = sizeof(*ops) * count;
   size_t req_len = sizeof(struct asahi_ccmd_vm_bind_req) + payload_size;
   struct asahi_ccmd_vm_bind_req *req = calloc(1, req_len);

   *req = (struct asahi_ccmd_vm_bind_req){
      .hdr.cmd = ASAHI_CCMD_VM_BIND,
      .hdr.len = req_len,
      .vm_id = dev->vm_id,
      .stride = sizeof(*ops),
      .count = count,
   };

   memcpy(req->payload, ops, payload_size);

   int ret = vdrm_send_req(dev->vdrm, &req->hdr, false);
   free(req);
   if (ret) {
      fprintf(stderr, "ASAHI_CCMD_GEM_BIND failed: %d\n", ret);
   }

   return ret;
}

static int
agx_virtio_bo_bind_object(struct agx_device *dev,
                          struct drm_asahi_gem_bind_object *bind)
{
   struct asahi_ccmd_gem_bind_object_req req = {
      .hdr.cmd = ASAHI_CCMD_GEM_BIND_OBJECT,
      .hdr.len = sizeof(struct asahi_ccmd_gem_bind_object_req),
      .bind = *bind,
   };

   struct asahi_ccmd_gem_bind_object_rsp *rsp;

   rsp = vdrm_alloc_rsp(dev->vdrm, &req.hdr,
                        sizeof(struct asahi_ccmd_gem_bind_object_rsp));

   int ret = vdrm_send_req(dev->vdrm, &req.hdr, true);
   if (ret || rsp->ret) {
      fprintf(stderr,
              "ASAHI_CCMD_GEM_BIND_OBJECT bind failed: %d:%d (handle=%d)\n",
              ret, rsp->ret, bind->handle);
   }

   if (!rsp->ret)
      bind->object_handle = rsp->object_handle;

   return rsp->ret;
}

static int
agx_virtio_bo_unbind_object(struct agx_device *dev, uint32_t object_handle)
{
   struct asahi_ccmd_gem_bind_object_req req = {
      .hdr.cmd = ASAHI_CCMD_GEM_BIND_OBJECT,
      .hdr.len = sizeof(struct asahi_ccmd_gem_bind_object_req),
      .bind = {
         .op = DRM_ASAHI_BIND_OBJECT_OP_UNBIND,
         .object_handle = object_handle,
      }};

   int ret = vdrm_send_req(dev->vdrm, &req.hdr, false);
   if (ret) {
      fprintf(stderr,
              "ASAHI_CCMD_GEM_BIND_OBJECT unbind failed: %d (handle=%d)\n", ret,
              object_handle);
   }

   return 0;
}

static void
agx_virtio_bo_mmap(struct agx_device *dev, struct agx_bo *bo, void *fixed_addr)
{
   bo->_map = vdrm_bo_map(dev->vdrm, bo->handle, bo->size, fixed_addr);
   if (bo->_map == MAP_FAILED) {
      bo->_map = NULL;
      fprintf(stderr, "mmap failed: result=%p size=0x%llx fd=%i\n", bo->_map,
              (long long)bo->size, dev->fd);
   }
}

static ssize_t
agx_virtio_get_params(struct agx_device *dev, void *buf, size_t size)
{
   struct vdrm_device *vdrm = dev->vdrm;
   struct asahi_ccmd_get_params_req req = {
      .params.size = size,
      .hdr.cmd = ASAHI_CCMD_GET_PARAMS,
      .hdr.len = sizeof(struct asahi_ccmd_get_params_req),
   };
   struct asahi_ccmd_get_params_rsp *rsp;

   rsp = vdrm_alloc_rsp(vdrm, &req.hdr,
                        sizeof(struct asahi_ccmd_get_params_rsp) + size);

   int ret = vdrm_send_req(vdrm, &req.hdr, true);
   if (ret || rsp->ret)
      return ret ? ret : rsp->ret;

   memcpy(buf, &rsp->payload, size);
   return size;
}

static int
agx_virtio_submit(struct agx_device *dev, struct drm_asahi_submit *submit,
                  struct agx_submit_virt *virt)
{
   struct drm_asahi_sync *syncs =
      (struct drm_asahi_sync *)(uintptr_t)submit->syncs;
   size_t req_len = sizeof(struct asahi_ccmd_submit_req) + submit->cmdbuf_size;

   size_t extres_size =
      sizeof(struct asahi_ccmd_submit_res) * virt->extres_count;
   req_len += extres_size;

   struct asahi_ccmd_submit_req *req =
      (struct asahi_ccmd_submit_req *)calloc(1, req_len);

   req->queue_id = submit->queue_id;
   req->extres_count = virt->extres_count;
   req->cmdbuf_size = submit->cmdbuf_size;

   char *ptr = (char *)&req->payload;

   memcpy(ptr, (void *)(uintptr_t)submit->cmdbuf, req->cmdbuf_size);
   ptr += req->cmdbuf_size;

   memcpy(ptr, virt->extres, extres_size);
   ptr += extres_size;

   req->hdr.cmd = ASAHI_CCMD_SUBMIT;
   req->hdr.len = req_len;

   uint32_t total_syncs = submit->in_sync_count + submit->out_sync_count;
   struct drm_virtgpu_execbuffer_syncobj *vdrm_syncs =
      calloc(total_syncs, sizeof(struct drm_virtgpu_execbuffer_syncobj));
   for (int i = 0; i < total_syncs; i++) {
      vdrm_syncs[i].handle = syncs[i].handle;
      vdrm_syncs[i].point = syncs[i].timeline_value;
   }

   struct vdrm_execbuf_params p = {
      .ring_idx = virt->ring_idx,
      .req = &req->hdr,
      .num_in_syncobjs = submit->in_sync_count,
      .in_syncobjs = vdrm_syncs,
      .num_out_syncobjs = submit->out_sync_count,
      .out_syncobjs = vdrm_syncs + submit->in_sync_count,
   };

   int ret = vdrm_execbuf(dev->vdrm, &p);

   free(vdrm_syncs);
   free(req);
   return ret;
}

const agx_device_ops_t agx_virtio_device_ops = {
   .bo_alloc = agx_virtio_bo_alloc,
   .bo_bind = agx_virtio_bo_bind,
   .bo_mmap = agx_virtio_bo_mmap,
   .get_params = agx_virtio_get_params,
   .submit = agx_virtio_submit,
   .bo_bind_object = agx_virtio_bo_bind_object,
   .bo_unbind_object = agx_virtio_bo_unbind_object,
};

bool
agx_virtio_open_device(struct agx_device *dev)
{
   struct vdrm_device *vdrm;

   vdrm = vdrm_device_connect(dev->fd, 4);
   if (!vdrm) {
      fprintf(stderr, "could not connect vdrm\n");
      return false;
   }

   dev->vdrm = vdrm;
   dev->ops = agx_virtio_device_ops;
   return true;
}
