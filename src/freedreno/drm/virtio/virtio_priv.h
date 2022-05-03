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

#ifndef VIRTIO_PRIV_H_
#define VIRTIO_PRIV_H_

#include <poll.h>

#include "freedreno_priv.h"

#include "util/u_atomic.h"
#include "util/slab.h"
#include "util/timespec.h"

#include "pipe/p_defines.h"

#include "drm-uapi/virtgpu_drm.h"
/* We also use some types/defines from the host drm/msm uabi: */
#include "drm-uapi/msm_drm.h"

#define VIRGL_RENDERER_UNSTABLE_APIS 1
#include "virglrenderer_hw.h"
#include "msm_proto.h"

struct virtio_device {
   struct fd_device base;

   struct fd_bo *shmem_bo;
   struct msm_shmem *shmem;
   uint32_t next_rsp_off;
   simple_mtx_t rsp_lock;
   simple_mtx_t eb_lock;

   uint32_t next_blob_id;
   uint32_t next_seqno;
};
FD_DEFINE_CAST(fd_device, virtio_device);

struct fd_device *virtio_device_new(int fd, drmVersionPtr version);

struct virtio_pipe {
   struct fd_pipe base;
   uint32_t pipe;
   uint32_t gpu_id;
   uint64_t chip_id;
   uint64_t gmem_base;
   uint32_t gmem;
   uint32_t queue_id;
   struct slab_parent_pool ring_pool;

   /**
    * If we *ever* see an in-fence-fd, assume that userspace is
    * not relying on implicit fences.
    */
   bool no_implicit_sync;

   /**
    * We know that the kernel allocated fence seqno's sequentially per-
    * submitqueue in a range 1..INT_MAX, which is incremented *after* any
    * point where the submit ioctl could be restarted.  So we just *guess*
    * what the next seqno fence will be to avoid having to synchronize the
    * submit with the host.
    *
    * TODO maybe we need version_minor bump so we can make the 1..INT_MAX
    * assumption.. it is only really true after:
    *
    *   ca3ffcbeb0c8 ("drm/msm/gpu: Don't allow zero fence_id")
    */
   int32_t next_submit_fence;
};
FD_DEFINE_CAST(fd_pipe, virtio_pipe);

struct fd_pipe *virtio_pipe_new(struct fd_device *dev, enum fd_pipe_id id,
                                uint32_t prio);

struct fd_submit *virtio_submit_new(struct fd_pipe *pipe);

struct virtio_bo {
   struct fd_bo base;
   uint64_t offset;

   uint32_t host_handle;
   uint32_t blob_id;
};
FD_DEFINE_CAST(fd_bo, virtio_bo);

struct fd_bo *virtio_bo_new(struct fd_device *dev, uint32_t size, uint32_t flags);
struct fd_bo *virtio_bo_from_handle(struct fd_device *dev, uint32_t size,
                                    uint32_t handle);

/*
 * Internal helpers:
 */
void *virtio_alloc_rsp(struct fd_device *dev, struct msm_ccmd_req *hdr, uint32_t sz);
int virtio_execbuf_fenced(struct fd_device *dev, struct msm_ccmd_req *req,
                          int in_fence_fd, int *out_fence_fd);
int virtio_execbuf(struct fd_device *dev, struct msm_ccmd_req *req, bool sync);
void virtio_host_sync(struct fd_device *dev, const struct msm_ccmd_req *req);
int virtio_simple_ioctl(struct fd_device *dev, unsigned cmd, void *req);

#endif /* VIRTIO_PRIV_H_ */
