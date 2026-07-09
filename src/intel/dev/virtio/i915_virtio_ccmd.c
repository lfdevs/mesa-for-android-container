/*
 * Copyright 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "intel_virtio_priv.h"
#include "i915_proto.h"

static int
i915_virtio_simple_ioctl(struct intel_virtio_device *dev,
                          unsigned cmd, void *_req)
{
   unsigned req_len = sizeof(struct i915_ccmd_ioctl_simple_req);
   unsigned rsp_len = sizeof(struct i915_ccmd_ioctl_simple_rsp);
   bool sync = !!(cmd & IOC_OUT);
   int err;

   req_len += _IOC_SIZE(cmd);
   if (cmd & IOC_OUT)
      rsp_len += _IOC_SIZE(cmd);

   uint8_t buf[req_len];
   struct i915_ccmd_ioctl_simple_req *req = (void *)(uintptr_t)buf;
   struct i915_ccmd_ioctl_simple_rsp *rsp;

   req->hdr = I915_CCMD(IOCTL_SIMPLE, req_len);
   req->cmd = cmd;
   memcpy(req->payload, _req, _IOC_SIZE(cmd));

   rsp = vdrm_alloc_rsp(dev->vdrm, &req->hdr, rsp_len);

   err = vdrm_send_req(dev->vdrm, &req->hdr, sync);
   if (err)
      return errno;

   if (cmd & IOC_OUT) {
      memcpy(_req, rsp->payload, _IOC_SIZE(cmd));
      return rsp->ret;
   }

   return 0;
}

static int
i915_virtio_queryparam(struct intel_virtio_device *dev,
                       struct drm_i915_query *query)
{
   struct drm_i915_query_item *item = (void *)(uintptr_t)query->items_ptr;
   struct i915_ccmd_queryparam_rsp *rsp;
   struct i915_ccmd_queryparam_req req;
   int err;

   if (query->num_items != 1) {
      mesa_loge("unsupported number of query items");
      return EINVAL;
   }

   req.hdr = I915_CCMD(QUERYPARAM, sizeof(req));
   req.query_id = item->query_id;
   req.length = item->length;
   req.flags = item->flags;

   rsp = vdrm_alloc_rsp(dev->vdrm, &req.hdr, sizeof(*rsp) + item->length);

   err = vdrm_send_req(dev->vdrm, &req.hdr, true);
   if (err)
      return errno;

   if (item->data_ptr && rsp->length > 0)
      memcpy((void *)(uintptr_t)item->data_ptr, rsp->payload, rsp->length);

   item->length = rsp->length;

   return rsp->ret;
}

static int
i915_virtio_getparam(struct intel_virtio_device *dev,
                     struct drm_i915_getparam *gp)
{
   struct i915_ccmd_getparam_rsp *rsp;
   struct i915_ccmd_getparam_req req;
   int err;

   req.hdr = I915_CCMD(GETPARAM, sizeof(req));
   req.param = gp->param;

   rsp = vdrm_alloc_rsp(dev->vdrm, &req.hdr, sizeof(*rsp));

   err = vdrm_send_req(dev->vdrm, &req.hdr, true);
   if (err)
      return errno;

   *gp->value = rsp->value;

   return rsp->ret;
}

static int
i915_virtio_gem_create(struct intel_virtio_device *dev,
                       struct drm_i915_gem_create *create)
{
   uint32_t blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE |
                         VIRTGPU_BLOB_FLAG_USE_SHAREABLE;

   if (dev->vdrm->supports_cross_device)
      blob_flags |= VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE;

   struct i915_ccmd_gem_create_req req = {
      .hdr = I915_CCMD(GEM_CREATE, sizeof(req)),
      .size = create->size,
   };

   /* tunneled cmds are processed separately on host side,
    * before the renderer->get_blob() callback.. the blob_id
    * is used to like the created bo to the get_blob() call
    */
   req.blob_id = p_atomic_inc_return(&dev->next_blob_id);

   uint32_t blob_hints = DRM_VIRTGPU_BLOB_FLAG_HINT_DEFER_MAPPING;

   int ret = vdrm_bo_create(dev->vdrm, create->size, blob_flags,
                            req.blob_id, blob_hints, &req.hdr);
   if (!ret)
      return EINVAL;

   create->handle = ret;

   return 0;
}

static int
i915_virtio_gem_create_ext(struct intel_virtio_device *dev,
                           struct drm_i915_gem_create_ext *create)
{
   struct i915_user_extension *extension = (void *)(uintptr_t)create->extensions;
   unsigned ext_size = 0;
   void *payload_ptr;

   while (extension) {
      switch (extension->name) {
      case I915_GEM_CREATE_EXT_MEMORY_REGIONS:
      {
         struct drm_i915_gem_create_ext_memory_regions *mem_regions;

         mem_regions = (void*)(uintptr_t)extension;
         ext_size += sizeof(*mem_regions);
         ext_size += sizeof(struct drm_i915_gem_memory_class_instance) * mem_regions->num_regions;
         break;
      }

      case I915_GEM_CREATE_EXT_PROTECTED_CONTENT:
         ext_size += sizeof(struct drm_i915_gem_create_ext_protected_content);
         break;

      case I915_GEM_CREATE_EXT_SET_PAT:
         ext_size += sizeof(struct drm_i915_gem_create_ext_set_pat);
         break;

      default:
         mesa_loge("unsupported extension %d", extension->name);
         return EINVAL;
      }

      extension = (void *)(uintptr_t)extension->next_extension;
   }

   unsigned req_len = sizeof(struct i915_ccmd_gem_create_ext_req);
   req_len += ext_size;

   uint8_t buf[req_len];
   struct i915_ccmd_gem_create_ext_req *req = (void *)(uintptr_t)buf;

   extension = (void *)(uintptr_t)create->extensions;
   payload_ptr = req->payload;

   while (extension) {
      switch (extension->name) {
      case I915_GEM_CREATE_EXT_MEMORY_REGIONS:
      {
         struct drm_i915_gem_create_ext_memory_regions *mem_regions;
         struct drm_i915_gem_memory_class_instance *instances;

         mem_regions = (void*)(uintptr_t)extension;
         instances = (void*)(uintptr_t)mem_regions->regions;

         memcpy(payload_ptr, mem_regions, sizeof(*mem_regions));
         payload_ptr += sizeof(*mem_regions);

         memcpy(payload_ptr, instances, sizeof(*instances) * mem_regions->num_regions);
         payload_ptr += sizeof(*instances) * mem_regions->num_regions;
         break;
      }

      case I915_GEM_CREATE_EXT_PROTECTED_CONTENT:
         memcpy(payload_ptr, extension, sizeof(struct drm_i915_gem_create_ext_protected_content));
         payload_ptr += sizeof(struct drm_i915_gem_create_ext_protected_content);
         break;

      case I915_GEM_CREATE_EXT_SET_PAT:
         memcpy(payload_ptr, extension, sizeof(struct drm_i915_gem_create_ext_set_pat));
         payload_ptr += sizeof(struct drm_i915_gem_create_ext_set_pat);
         break;

      default:
         mesa_loge("unsupported extension");
         return EINVAL;
      }

      extension = (void *)(uintptr_t)extension->next_extension;
   }

   req->hdr = I915_CCMD(GEM_CREATE_EXT, req_len);
   req->gem_flags = create->flags;
   req->ext_size = ext_size;
   req->size = create->size;

   uint32_t blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE |
                         VIRTGPU_BLOB_FLAG_USE_SHAREABLE;

   if (dev->vdrm->supports_cross_device)
      blob_flags |= VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE;

   /* tunneled cmds are processed separately on host side,
    * before the renderer->get_blob() callback.. the blob_id
    * is used to like the created bo to the get_blob() call
    */
   req->blob_id = p_atomic_inc_return(&dev->next_blob_id);

   uint32_t blob_hints = DRM_VIRTGPU_BLOB_FLAG_HINT_DEFER_MAPPING;

   int ret = vdrm_bo_create(dev->vdrm, create->size, blob_flags,
                            req->blob_id, blob_hints, &req->hdr);
   if (!ret)
      return EINVAL;

   create->handle = ret;

   return 0;
}

static int
i915_virtio_gem_close(struct intel_virtio_device *dev,
                      struct drm_gem_close *close)
{
   vdrm_bo_close(dev->vdrm, close->handle);
   return 0;
}

static int
i915_virtio_gem_context_create_ext(struct intel_virtio_device *dev,
                                   struct drm_i915_gem_context_create_ext *create)
{
   struct drm_i915_gem_context_create_ext_setparam *setparam;
   struct i915_ccmd_gem_context_create_rsp *rsp;
   unsigned params_size = 0;
   void *payload_ptr;
   int err;

   if (!(create->flags & I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS))
      return i915_virtio_simple_ioctl(dev, DRM_IOCTL_I915_GEM_CONTEXT_CREATE,
                                      create);

   setparam = (void *)(uintptr_t)create->extensions;

   while (setparam) {
      switch (setparam->param.param) {
      case I915_CONTEXT_PARAM_BAN_PERIOD:
      case I915_CONTEXT_PARAM_NO_ZEROMAP:
      case I915_CONTEXT_PARAM_GTT_SIZE:
      case I915_CONTEXT_PARAM_NO_ERROR_CAPTURE:
      case I915_CONTEXT_PARAM_BANNABLE:
      case I915_CONTEXT_PARAM_PRIORITY:
      case I915_CONTEXT_PARAM_SSEU:
      case I915_CONTEXT_PARAM_RECOVERABLE:
      case I915_CONTEXT_PARAM_VM:
      case I915_CONTEXT_PARAM_ENGINES:
      case I915_CONTEXT_PARAM_PERSISTENCE:
      case I915_CONTEXT_PARAM_RINGSIZE:
      case I915_CONTEXT_PARAM_PROTECTED_CONTENT:
         break;

      default:
         mesa_loge("unsupported context param");
         return EINVAL;
      }

      params_size += sizeof(*setparam) + setparam->param.size;
      setparam = (void *)(uintptr_t)setparam->base.next_extension;
   }

   unsigned req_len = sizeof(struct i915_ccmd_gem_context_create_req);
   req_len += params_size;

   uint8_t buf[req_len];
   struct i915_ccmd_gem_context_create_req *req = (void *)(uintptr_t)buf;

   setparam = (void *)(uintptr_t)create->extensions;
   payload_ptr = req->payload;

   while (setparam) {
      memcpy(payload_ptr, setparam, sizeof(*setparam));
      payload_ptr += sizeof(*setparam);

      if (setparam->param.size) {
         memcpy(payload_ptr, (void*)(uintptr_t)setparam->param.value,
                setparam->param.size);
         payload_ptr += setparam->param.size;
      }

      setparam = (void *)(uintptr_t)setparam->base.next_extension;
   }

   req->hdr = I915_CCMD(GEM_CONTEXT_CREATE, req_len);
   req->params_size = params_size;
   req->flags = create->flags;

   rsp = vdrm_alloc_rsp(dev->vdrm, &req->hdr, sizeof(*rsp));

   err = vdrm_send_req(dev->vdrm, &req->hdr, true);
   if (err)
      return errno;

   create->ctx_id = rsp->ctx_id;

   return rsp->ret;
}

static int
i915_virtio_gem_context_param(struct intel_virtio_device *dev,
                              unsigned long cmd,
                              struct drm_i915_gem_context_param *param)
{
   switch (param->param) {
   case I915_CONTEXT_PARAM_RECOVERABLE:
   case I915_CONTEXT_PARAM_PRIORITY:
   case I915_CONTEXT_PARAM_GTT_SIZE:
   case I915_CONTEXT_PARAM_VM:
      return i915_virtio_simple_ioctl(dev, cmd, param);

   default:
      mesa_loge("unsupported context param");
      return EINVAL;
   }
}

static int
intel_virtio_ioctl_errno(int fd, unsigned long cmd, void *req)
{
   int err = ioctl(fd, cmd, req);
   if (!err)
      errno = 0;

   return errno;
}

static int
i915_virtio_gem_busy(struct intel_virtio_device *dev,
                     struct drm_i915_gem_busy *busy)
{
   struct drm_virtgpu_3d_wait virt_wait = {
      .handle = busy->handle,
      .flags = VIRTGPU_WAIT_NOWAIT,
   };

   intel_virtio_ioctl_errno(dev->fd, DRM_IOCTL_VIRTGPU_WAIT, &virt_wait);

   if (errno == EBUSY) {
      errno = 0;
      busy->busy = 1;
   } else if (!errno) {
      busy->busy = 0;
   }

   return errno;
}

static int
i915_virtio_gem_wait(struct intel_virtio_device *dev,
                     struct drm_i915_gem_wait *wait)
{
   struct drm_virtgpu_3d_wait virt_wait = { .handle = wait->bo_handle };

   if (!wait->timeout_ns)
      virt_wait.flags = VIRTGPU_WAIT_NOWAIT;

   intel_virtio_ioctl_errno(dev->fd, DRM_IOCTL_VIRTGPU_WAIT, &virt_wait);

   if (errno == EBUSY)
      errno = ETIME;

   return errno;
}

static int
i915_virtio_gem_busy_vpipe(struct intel_virtio_device *dev,
                           struct drm_i915_gem_busy *busy)
{
   struct i915_ccmd_gem_busy_rsp *rsp;
   struct i915_ccmd_gem_busy_req req;
   int err;

   req.hdr = I915_CCMD(GEM_BUSY, sizeof(req));
   req.res_id = vdrm_handle_to_res_id(dev->vdrm, busy->handle);

   rsp = vdrm_alloc_rsp(dev->vdrm, &req.hdr, sizeof(*rsp));

   err = vdrm_send_req(dev->vdrm, &req.hdr, true);
   if (err)
      return errno;

   busy->busy = (rsp->busy != 0);

   return rsp->ret;
}

static int64_t time_ns(void)
{
   struct timespec ts;

   clock_gettime(CLOCK_MONOTONIC, &ts);

   return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int
i915_virtio_gem_wait_vpipe(struct intel_virtio_device *dev,
                           struct drm_i915_gem_wait *wait)
{
   do {
      struct drm_i915_gem_busy busy = { .handle = wait->bo_handle };
      int64_t start_time = time_ns();

      int err = i915_virtio_gem_busy_vpipe(dev, &busy);
      if (err)
         return err;

      wait->timeout_ns -= time_ns() - start_time;
      if (wait->timeout_ns < 0)
         wait->timeout_ns = 0;

      if (!busy.busy)
         return 0;

      if (wait->timeout_ns)
         sched_yield();

   } while (wait->timeout_ns);

   return ETIME;
}

static int
i915_virtio_simple_ioctl_gem_patched(struct intel_virtio_device *dev,
                                     unsigned long cmd, void *req)
{
   uint32_t *handle = req;
   uint32_t tmp_handle = *handle;

   *handle = vdrm_handle_to_res_id(dev->vdrm, *handle);
   errno = i915_virtio_simple_ioctl(dev, cmd, req);
   *handle = tmp_handle;

   return errno;
}

static int
i915_virtio_gem_vm_control(struct intel_virtio_device *dev,
                           unsigned long cmd,
                           struct drm_i915_gem_vm_control *vm)
{
   if (vm->extensions) {
      mesa_loge("unsupported vm extension");
      return EINVAL;
   }

   if (vm->flags) {
      mesa_loge("unsupported vm flags");
      return EINVAL;
   }

   return i915_virtio_simple_ioctl(dev, cmd, vm);
}

static int
i915_virtio_get_reset_stats(struct intel_virtio_device *dev,
                            unsigned long cmd,
                            struct drm_i915_reset_stats *stats)
{
   struct i915_shmem *shmem = to_i915_shmem(dev->vdrm->shmem);

   if (stats->ctx_id >= 64 ||
       !(p_atomic_read(&shmem->banned_ctx_mask) & (1ULL << stats->ctx_id)))
      return 0;

   errno = i915_virtio_simple_ioctl(dev, cmd, stats);

   return errno;
}

static int
i915_virtio_gem_mmap_offset(struct intel_virtio_device *dev,
                            unsigned long cmd,
                            struct drm_i915_gem_mmap_offset *mmap_offset)
{
   struct i915_ccmd_gem_set_mmap_mode_req req;
   int err;

   req.hdr = I915_CCMD(GEM_SET_MMAP_MODE, sizeof(req));
   req.res_id = vdrm_handle_to_res_id(dev->vdrm, mmap_offset->handle);
   req.flags = mmap_offset->flags;

   err = vdrm_send_req(dev->vdrm, &req.hdr, false);
   if (err)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_wait(struct intel_virtio_device *dev,
                               struct drm_syncobj_wait *args)
{
   int ret = dev->sync->wait(dev->sync,
                             (uint32_t*)(uintptr_t)args->handles,
                             args->count_handles,
                             args->timeout_nsec, args->flags,
                             &args->first_signaled);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_create(struct intel_virtio_device *dev,
                                 struct drm_syncobj_create *args)
{
   int ret = dev->sync->create(dev->sync, args->flags, &args->handle);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_destroy(struct intel_virtio_device *dev,
                                  struct drm_syncobj_destroy *args)
{
   int ret = dev->sync->destroy(dev->sync, args->handle);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_reset(struct intel_virtio_device *dev,
                                struct drm_syncobj_array *args)
{
   int ret = dev->sync->reset(dev->sync, (uint32_t*)(uintptr_t)args->handles,
                              args->count_handles);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_signal(struct intel_virtio_device *dev,
                                 struct drm_syncobj_array *args)
{
   int ret = dev->sync->signal(dev->sync, (uint32_t*)(uintptr_t)args->handles,
                               args->count_handles);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_timeline_signal(struct intel_virtio_device *dev,
                                          struct drm_syncobj_timeline_array *args)
{
   int ret = dev->sync->timeline_signal(dev->sync,
                                        (uint32_t*)(uintptr_t)args->handles,
                                        (uint64_t*)(uintptr_t)args->points,
                                        args->count_handles);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_timeline_wait(struct intel_virtio_device *dev,
                                        struct drm_syncobj_timeline_wait *args)
{
   int ret = dev->sync->timeline_wait(dev->sync,
                                      (uint32_t*)(uintptr_t)args->handles,
                                      (uint64_t*)(uintptr_t)args->points,
                                      args->count_handles,
                                      args->timeout_nsec, args->flags,
                                      &args->first_signaled);
   if (ret < 0)
      return -ret;

   return 0;
}

static int
intel_virtio_sync_syncobj_transfer(struct intel_virtio_device *dev,
                                   struct drm_syncobj_transfer *args)
{
   int ret = dev->sync->transfer(dev->sync, args->dst_handle, args->dst_point,
                                 args->src_handle, args->src_point, args->flags);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_query(struct intel_virtio_device *dev,
                                struct drm_syncobj_timeline_array *args)
{
   int ret = dev->sync->query(dev->sync,
                              (uint32_t*)(uintptr_t)args->handles,
                              (uint64_t*)(uintptr_t)args->points,
                              args->count_handles,
                              args->flags);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_fd_to_handle(struct intel_virtio_device *dev,
                                       unsigned long cmd,
                                       struct drm_syncobj_handle *args)
{
   if (args->flags)
      assert(!dev->vpipe);

   if (!dev->vpipe)
      return intel_virtio_ioctl_errno(dev->fd, cmd, args);

   int ret = dev->sync->fd_to_handle(dev->sync, args->fd, &args->handle);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_sync_syncobj_handle_to_fd(struct intel_virtio_device *dev,
                                       unsigned long cmd,
                                       struct drm_syncobj_handle *args)
{
   if (args->flags)
      assert(!dev->vpipe);

   if (!dev->vpipe)
      return intel_virtio_ioctl_errno(dev->fd, cmd, args);

   int ret = dev->sync->handle_to_fd(dev->sync, args->handle, &args->fd);
   if (ret < 0)
      return errno;

   return 0;
}

static int
intel_virtio_prime_fd_to_handle(struct intel_virtio_device *dev,
                                struct drm_prime_handle *args)
{
   args->handle = vdrm_dmabuf_to_handle(dev->vdrm, args->fd);
   if (!args->handle) {
      errno = EINVAL;
      return errno;
   }

   return 0;
}

static int
intel_virtio_prime_handle_to_fd(struct intel_virtio_device *dev,
                                struct drm_prime_handle *args)
{
   args->fd = vdrm_bo_export_dmabuf(dev->vdrm, args->handle);
   if (args->fd < 0) {
      errno = EINVAL;
      return errno;
   }

   return 0;
}

int
intel_virtio_ioctl(int fd, unsigned long cmd, void *req)
{
   int orig_errno = errno;
   struct intel_virtio_device *dev = fd_to_intel_virtio_device(fd);

   if (!dev) {
      errno = orig_errno;
      /* this is a real phys device if not bound to virtio */
      return ioctl(fd, cmd, req);
   }

   /*
    * Special case for legacy ioctls that have same NR as extended ioctl
    * and need to be handled differently.
    */
   switch (cmd) {
   case DRM_IOCTL_I915_GEM_CREATE:
      errno = i915_virtio_gem_create(dev, req);
      goto out;

   case DRM_IOCTL_I915_GEM_CONTEXT_CREATE:
      errno = i915_virtio_simple_ioctl(dev, cmd, req);
      goto out;

   default:
      break;
   }

#define IOC_MASKED(IOC) ((IOC) & ~IOCSIZE_MASK)

   /* DRM ioctls vary in size depending on a used UAPI header version */
   switch (IOC_MASKED(cmd)) {
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_WAIT):
      errno = intel_virtio_sync_syncobj_wait(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_CREATE):
      errno = intel_virtio_sync_syncobj_create(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_DESTROY):
      errno = intel_virtio_sync_syncobj_destroy(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_RESET):
      errno = intel_virtio_sync_syncobj_reset(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_SIGNAL):
      errno = intel_virtio_sync_syncobj_signal(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL):
      errno = intel_virtio_sync_syncobj_timeline_signal(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT):
      errno = intel_virtio_sync_syncobj_timeline_wait(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_TRANSFER):
      errno = intel_virtio_sync_syncobj_transfer(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_QUERY):
      errno = intel_virtio_sync_syncobj_query(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE):
      errno = intel_virtio_sync_syncobj_fd_to_handle(dev, cmd, req);
      break;
   case IOC_MASKED(DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD):
      errno = intel_virtio_sync_syncobj_handle_to_fd(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_PRIME_HANDLE_TO_FD):
      errno = intel_virtio_prime_handle_to_fd(dev, req);
      break;
   case IOC_MASKED(DRM_IOCTL_PRIME_FD_TO_HANDLE):
      errno = intel_virtio_prime_fd_to_handle(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_CREATE_EXT):
      errno = i915_virtio_gem_create_ext(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GETPARAM):
      errno = i915_virtio_getparam(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_QUERY):
      errno = i915_virtio_queryparam(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_GEM_CLOSE):
      errno = i915_virtio_gem_close(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT):
      errno = i915_virtio_gem_context_create_ext(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM):
   case IOC_MASKED(DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM):
      errno = i915_virtio_gem_context_param(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_EXECBUFFER2):
      errno = i915_virtio_gem_execbuffer2(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_MADVISE):
      errno = 0;
      break;
   case IOC_MASKED(DRM_IOCTL_I915_GET_RESET_STATS):
      errno = i915_virtio_get_reset_stats(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_REG_READ):
   case IOC_MASKED(DRM_IOCTL_I915_GEM_CONTEXT_DESTROY):
   case IOC_MASKED(DRM_IOCTL_I915_GEM_GET_APERTURE):
      errno = i915_virtio_simple_ioctl(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_GET_TILING):
   case IOC_MASKED(DRM_IOCTL_I915_GEM_SET_TILING):
   case IOC_MASKED(DRM_IOCTL_I915_GEM_SET_DOMAIN):
      errno = i915_virtio_simple_ioctl_gem_patched(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_BUSY):
      /*
       * vpipe doesn't support tracking busy GEMs, use GEM-busy CCMD that
       * is added specifically for vpipe purposes.
       */
      if (dev->vpipe)
         errno = i915_virtio_gem_busy_vpipe(dev, req);
      else
         errno = i915_virtio_gem_busy(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_WAIT):
      if (dev->vpipe)
         errno = i915_virtio_gem_wait_vpipe(dev, req);
      else
         errno = i915_virtio_gem_wait(dev, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_VM_CREATE):
   case IOC_MASKED(DRM_IOCTL_I915_GEM_VM_DESTROY):
      errno = i915_virtio_gem_vm_control(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_MMAP_OFFSET):
      errno = i915_virtio_gem_mmap_offset(dev, cmd, req);
      break;

   case IOC_MASKED(DRM_IOCTL_I915_GEM_USERPTR):
      errno = ENODEV;
      break;

   default:
      mesa_loge("unsupported ioctl 0x%lx\n", _IOC_NR(cmd));
      errno = ENOTTY;
      break;
   }

#undef IOC_MASKED

out:
   if (errno) {
      mesa_logd("ioctl 0x%lx failed errno=%d\n", _IOC_NR(cmd), errno);
      return -1;
   }

   errno = orig_errno;

   return 0;
}
