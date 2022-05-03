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

#include "util/slab.h"

#include "freedreno_ringbuffer_sp.h"
#include "virtio_priv.h"

static int
query_param(struct fd_pipe *pipe, uint32_t param, uint64_t *value)
{
   struct virtio_pipe *virtio_pipe = to_virtio_pipe(pipe);
   struct drm_msm_param req = {
      .pipe = virtio_pipe->pipe,
      .param = param,
   };
   int ret;

   ret = virtio_simple_ioctl(pipe->dev, DRM_IOCTL_MSM_GET_PARAM, &req);
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

static int
query_queue_param(struct fd_pipe *pipe, uint32_t param, uint64_t *value)
{
   struct msm_ccmd_submitqueue_query_req req = {
         .hdr = MSM_CCMD(SUBMITQUEUE_QUERY, sizeof(req)),
         .queue_id = to_virtio_pipe(pipe)->queue_id,
         .param = param,
         .len = sizeof(*value),
   };
   struct msm_ccmd_submitqueue_query_rsp *rsp;
   unsigned rsp_len = sizeof(*rsp) + req.len;

   rsp = virtio_alloc_rsp(pipe->dev, &req.hdr, rsp_len);

   int ret = virtio_execbuf(pipe->dev, &req.hdr, true);
   if (ret)
      goto out;

   memcpy(value, rsp->payload, req.len);

   ret = rsp->ret;

out:
   return ret;
}

static int
virtio_pipe_get_param(struct fd_pipe *pipe, enum fd_param_id param,
                   uint64_t *value)
{
   struct virtio_pipe *virtio_pipe = to_virtio_pipe(pipe);
   switch (param) {
   case FD_DEVICE_ID: // XXX probably get rid of this..
   case FD_GPU_ID:
      *value = virtio_pipe->gpu_id;
      return 0;
   case FD_GMEM_SIZE:
      *value = virtio_pipe->gmem;
      return 0;
   case FD_GMEM_BASE:
      *value = virtio_pipe->gmem_base;
      return 0;
   case FD_CHIP_ID:
      *value = virtio_pipe->chip_id;
      return 0;
   case FD_MAX_FREQ:
      return query_param(pipe, MSM_PARAM_MAX_FREQ, value);
   case FD_TIMESTAMP:
      return query_param(pipe, MSM_PARAM_TIMESTAMP, value);
   case FD_NR_RINGS:
      /* TODO need to not rely on host egl ctx for fence if
       * we want to support multiple priority levels
       */
      return 1;
//      return query_param(pipe, MSM_PARAM_NR_RINGS, value);
   case FD_PP_PGTABLE:
      return query_param(pipe, MSM_PARAM_PP_PGTABLE, value);
   case FD_CTX_FAULTS:
      return query_queue_param(pipe, MSM_SUBMITQUEUE_PARAM_FAULTS, value);
   case FD_GLOBAL_FAULTS:
      return query_param(pipe, MSM_PARAM_FAULTS, value);
   case FD_SUSPEND_COUNT:
      return query_param(pipe, MSM_PARAM_SUSPENDS, value);
   default:
      ERROR_MSG("invalid param id: %d", param);
      return -1;
   }
}

static int
virtio_pipe_wait(struct fd_pipe *pipe, const struct fd_fence *fence, uint64_t timeout)
{
   struct msm_ccmd_wait_fence_req req = {
         .hdr = MSM_CCMD(WAIT_FENCE, sizeof(req)),
         .queue_id = to_virtio_pipe(pipe)->queue_id,
         .fence = fence->kfence,
         .timeout = timeout,
   };
   struct msm_ccmd_submitqueue_query_rsp *rsp;

   rsp = virtio_alloc_rsp(pipe->dev, &req.hdr, sizeof(*rsp));

   int ret = virtio_execbuf(pipe->dev, &req.hdr, true);
   if (ret)
      goto out;

   ret = rsp->ret;

out:
   return ret;
}

static int
open_submitqueue(struct fd_pipe *pipe, uint32_t prio)
{
   struct drm_msm_submitqueue req = {
      .flags = 0,
      .prio = prio,
   };
   uint64_t nr_rings = 1;
   int ret;

   virtio_pipe_get_param(pipe, FD_NR_RINGS, &nr_rings);

   req.prio = MIN2(req.prio, MAX2(nr_rings, 1) - 1);

   ret = virtio_simple_ioctl(pipe->dev, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &req);
   if (ret) {
      ERROR_MSG("could not create submitqueue! %d (%s)", ret, strerror(errno));
      return ret;
   }

   to_virtio_pipe(pipe)->queue_id = req.id;

   return 0;
}

static void
close_submitqueue(struct fd_pipe *pipe, uint32_t queue_id)
{
   virtio_simple_ioctl(pipe->dev, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &queue_id);
}

static void
virtio_pipe_destroy(struct fd_pipe *pipe)
{
   struct virtio_pipe *virtio_pipe = to_virtio_pipe(pipe);

   close_submitqueue(pipe, virtio_pipe->queue_id);
   fd_pipe_sp_ringpool_fini(pipe);
   free(virtio_pipe);
}

static const struct fd_pipe_funcs funcs = {
   .ringbuffer_new_object = fd_ringbuffer_sp_new_object,
   .submit_new = virtio_submit_new,
   .flush = fd_pipe_sp_flush,
   .get_param = virtio_pipe_get_param,
   .wait = virtio_pipe_wait,
   .destroy = virtio_pipe_destroy,
};

static uint64_t
get_param(struct fd_pipe *pipe, uint32_t param)
{
   uint64_t value;
   int ret = query_param(pipe, param, &value);
   if (ret) {
      ERROR_MSG("get-param failed! %d (%s)", ret, strerror(errno));
      return 0;
   }
   return value;
}

static void
init_shmem(struct fd_device *dev)
{
   struct virtio_device *virtio_dev = to_virtio_device(dev);

   simple_mtx_lock(&virtio_dev->rsp_lock);

   /* One would like to do this in virtio_device_new(), but we'd
    * have to bypass/reinvent fd_bo_new()..
    */
   if (unlikely(!virtio_dev->shmem)) {
      virtio_dev->shmem_bo = fd_bo_new(dev, sizeof(*virtio_dev->shmem),
                                       _FD_BO_VIRTIO_SHM, "shmem");
      virtio_dev->shmem = fd_bo_map(virtio_dev->shmem_bo);

      virtio_dev->shmem_bo->bo_reuse = NO_CACHE;
   }

   simple_mtx_unlock(&virtio_dev->rsp_lock);
}

struct fd_pipe *
virtio_pipe_new(struct fd_device *dev, enum fd_pipe_id id, uint32_t prio)
{
   static const uint32_t pipe_id[] = {
      [FD_PIPE_3D] = MSM_PIPE_3D0,
      [FD_PIPE_2D] = MSM_PIPE_2D0,
   };
   struct virtio_pipe *virtio_pipe = NULL;
   struct fd_pipe *pipe = NULL;

   init_shmem(dev);

   virtio_pipe = calloc(1, sizeof(*virtio_pipe));
   if (!virtio_pipe) {
      ERROR_MSG("allocation failed");
      goto fail;
   }

   pipe = &virtio_pipe->base;

   pipe->funcs = &funcs;

   /* initialize before get_param(): */
   pipe->dev = dev;
   virtio_pipe->pipe = pipe_id[id];

   /* these params should be supported since the first version of drm/msm: */
   virtio_pipe->gpu_id = get_param(pipe, MSM_PARAM_GPU_ID);
   virtio_pipe->gmem = get_param(pipe, MSM_PARAM_GMEM_SIZE);
   virtio_pipe->chip_id = get_param(pipe, MSM_PARAM_CHIP_ID);

   if (fd_device_version(pipe->dev) >= FD_VERSION_GMEM_BASE)
      virtio_pipe->gmem_base = get_param(pipe, MSM_PARAM_GMEM_BASE);

   if (!(virtio_pipe->gpu_id || virtio_pipe->chip_id))
      goto fail;

   INFO_MSG("Pipe Info:");
   INFO_MSG(" GPU-id:          %d", virtio_pipe->gpu_id);
   INFO_MSG(" Chip-id:         0x%016"PRIx64, virtio_pipe->chip_id);
   INFO_MSG(" GMEM size:       0x%08x", virtio_pipe->gmem);

   if (open_submitqueue(pipe, prio))
      goto fail;

   fd_pipe_sp_ringpool_init(pipe);

   return pipe;
fail:
   if (pipe)
      fd_pipe_del(pipe);
   return NULL;
}
