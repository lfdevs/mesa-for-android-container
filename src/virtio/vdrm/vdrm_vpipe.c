/*
 * Copyright © 2023 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#define VIRGL_RENDERER_UNSTABLE_APIS 1

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <xf86drm.h>

#include "vdrm.h"

#include "drm-uapi/virtgpu_drm.h"
#include "util/os_file.h"
#include "util/libsync.h"
#include "util/log.h"
#include "util/os_time.h"
#include "util/perf/cpu_trace.h"
#include "util/u_dynarray.h"
#include "util/u_idalloc.h"
#include "util/u_process.h"
#include "util/u_sync_provider.h"

#include "vtest/vtest_protocol.h"

#define SHMEM_SZ 0x4000

struct vpipe_device {
   struct vdrm_device base;
   int sock_fd;
   simple_mtx_t lock;
   unsigned protocol_version;
   bool has_syncobj : 1;
   bool has_timeline_syncobj : 1;
   uint32_t shmem_res_id;

   /* used for allocating indexes into bo_table: */
   struct util_idalloc bo_idx_allocator;

   /* table of BO info: */
   struct util_dynarray bo_table;
};
DEFINE_CAST(vdrm_device, vpipe_device)

struct vpipe_bo {
   uint32_t res_id;
};

/*
 * Our fake handles are just indexes into the bo table plus one
 * (because zero is an invalid handle):
 */
static uint32_t idx2handle(uint32_t idx) { return idx + 1; }
static uint32_t handle2idx(uint32_t handle) { return handle - 1; }

static struct vpipe_bo *
handle2bo(struct vpipe_device *vtdev, uint32_t handle)
{
   simple_mtx_assert_locked(&vtdev->lock);
   return util_dynarray_element(&vtdev->bo_table, struct vpipe_bo, handle2idx(handle));
}

static int
vpipe_write(struct vpipe_device *vtdev, const void *buf, int size)
{
   simple_mtx_assert_locked(&vtdev->lock);
   const void *ptr = buf;
   int left;
   int ret;
   left = size;
   do {
      ret = write(vtdev->sock_fd, ptr, left);
      if (ret < 0)
         return -errno;
      left -= ret;
      ptr += ret;
   } while (left);
   return size;
}

static int
vpipe_read_fd(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   left = size;
   do {
      ret = read(fd, ptr, left);
      if (ret <= 0) {
         mesa_loge("lost connection to rendering server on %d read %d %d",
                   size, ret, errno);
         abort();
         return ret < 0 ? -errno : 0;
      }
      left -= ret;
      ptr += ret;
   } while (left);
   return size;
}

static int
vpipe_read(struct vpipe_device *vtdev, void *buf, int size)
{
   simple_mtx_assert_locked(&vtdev->lock);
   return vpipe_read_fd(vtdev->sock_fd, buf, size);
}

static int
vpipe_send_fd(struct vpipe_device *vtdev, int fd)
{
   simple_mtx_assert_locked(&vtdev->lock);
   struct iovec iovec;
   char buf[CMSG_SPACE(sizeof(int))];
   char c = 0;
   struct msghdr msgh = { 0 };
   memset(buf, 0, sizeof(buf));

   iovec.iov_base = &c;
   iovec.iov_len = sizeof(char);

   msgh.msg_name = NULL;
   msgh.msg_namelen = 0;
   msgh.msg_iov = &iovec;
   msgh.msg_iovlen = 1;
   msgh.msg_control = buf;
   msgh.msg_controllen = sizeof(buf);
   msgh.msg_flags = 0;

   struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
   cmsg->cmsg_level = SOL_SOCKET;
   cmsg->cmsg_type = SCM_RIGHTS;
   cmsg->cmsg_len = CMSG_LEN(sizeof(int));

   *((int *) CMSG_DATA(cmsg)) = fd;

   int size = sendmsg(vtdev->sock_fd, &msgh, 0);
   if (size < 0) {
      mesa_loge("Failed to send fd");
      return -1;
   }

   return 0;
}

static int
vpipe_receive_fd(struct vpipe_device *vtdev)
{
   simple_mtx_assert_locked(&vtdev->lock);
   struct cmsghdr *cmsgh;
   struct msghdr msgh = { 0 };
   char buf[CMSG_SPACE(sizeof(int))], c;
   struct iovec iovec;

   iovec.iov_base = &c;
   iovec.iov_len = sizeof(char);

   msgh.msg_name = NULL;
   msgh.msg_namelen = 0;
   msgh.msg_iov = &iovec;
   msgh.msg_iovlen = 1;
   msgh.msg_control = buf;
   msgh.msg_controllen = sizeof(buf);
   msgh.msg_flags = 0;

   int size = recvmsg(vtdev->sock_fd, &msgh, 0);
   if (size < 0) {
     mesa_loge("Failed with %s", strerror(errno));
     return -1;
   }

   cmsgh = CMSG_FIRSTHDR(&msgh);
   if (!cmsgh) {
     mesa_loge("No headers available");
     return -1;
   }

   if (cmsgh->cmsg_level != SOL_SOCKET) {
     mesa_loge("invalid cmsg_level %d", cmsgh->cmsg_level);
     return -1;
   }

   if (cmsgh->cmsg_type != SCM_RIGHTS) {
     mesa_loge("invalid cmsg_type %d", cmsgh->cmsg_type);
     return -1;
   }

   return *((int *) CMSG_DATA(cmsgh));
}

static uint32_t
vpipe_create_blob(struct vpipe_device *v, size_t size, uint32_t flags,
                  uint64_t blob_id, int *out_fd)
{
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_SIZE];
   uint32_t res_id = 0;
   /* If not mappable/shareable/exportable we don't need to keep the fd: */
   bool close_fd = !flags;

   MESA_TRACE_FUNC();

   size = ALIGN_POT(size, getpagesize());

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_RES_CREATE_BLOB_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_RESOURCE_CREATE_BLOB;

   vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_TYPE] = VCMD_BLOB_TYPE_HOST3D;

   /* Hack: vpipe tries to send us an fd whether we want it or not.. which
    * fails if it is not marked as exportable in some form:
    */
   flags &= ~VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
   flags |= VIRTGPU_BLOB_FLAG_USE_MAPPABLE;

   vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_FLAGS] = flags;
   vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_SIZE_LO] = (uint32_t)size;
   vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_SIZE_HI] =
      (uint32_t)((uint64_t)size >> 32);
   vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_ID_LO] = (uint32_t)blob_id;
   vcmd_res_create_blob[VCMD_RES_CREATE_BLOB_ID_HI] =
      (uint32_t)(blob_id >> 32);

   vpipe_write(v, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(v, vcmd_res_create_blob, sizeof(vcmd_res_create_blob));

   vpipe_read(v, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 1);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_RESOURCE_CREATE_BLOB);

   // TODO we'd prefer to allocate the res_id on client side to avoid sync
   // round trip:
   vpipe_read(v, &res_id, sizeof(uint32_t));

   // TODO we'd prefer to not get an fd back if we don't need to mmap or share
   // if we git rid of this and res_id readback, then we avoid the extra round
   // trip.. fd can be requested via VCMD_RESOURCE_EXPORT_FD
   *out_fd = vpipe_receive_fd(v);

   if (close_fd) {
      close(*out_fd);
      *out_fd = -1;
   }

   return res_id;
}

static void
close_res_id(struct vpipe_device *vtdev, uint32_t res_id)
{
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t cmd[1];
   vpipe_hdr[VTEST_CMD_LEN] = 1;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_RESOURCE_UNREF;

   MESA_TRACE_FUNC();

   cmd[0] = res_id;
   vpipe_write(vtdev, &vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vtdev, &cmd, sizeof(cmd));
}

static void
vpipe_submit_cmd(struct vpipe_device *vtdev,
                 const void *cmd, uint32_t cmdsize,
                 enum vcmd_submit_cmd2_flag flags,
                 uint32_t ring_idx, uintptr_t *fencep,
                 uint32_t num_in_syncobjs,
                 uint32_t num_out_syncobjs)
{
   simple_mtx_assert_locked(&vtdev->lock);
   MESA_TRACE_FUNC();

   const size_t header_size = sizeof(uint32_t) + sizeof(struct vcmd_submit_cmd2_batch);

   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   vpipe_hdr[VTEST_CMD_LEN] = (header_size + cmdsize) / sizeof(uint32_t);
   vpipe_hdr[VTEST_CMD_ID] = VCMD_SUBMIT_CMD2;
   vpipe_write(vtdev, vpipe_hdr, sizeof(vpipe_hdr));

   const uint32_t batch_count = 1;
   vpipe_write(vtdev, &batch_count, sizeof(batch_count));

   struct vcmd_submit_cmd2_batch dst = {
      .cmd_offset = header_size / sizeof(uint32_t),
      .cmd_size = cmdsize / sizeof(uint32_t),
      .flags = flags,
      .ring_idx = ring_idx,
      .num_in_syncobj = num_in_syncobjs,
      .num_out_syncobj = num_out_syncobjs,
   };
   vpipe_write(vtdev, &dst, sizeof(dst));

   vpipe_write(vtdev, cmd, cmdsize);

   // TODO if fencep!=NULL then we need a way to get a fence that
   // can be later waited on..
}

static int
vpipe_execbuf_locked(struct vdrm_device *vdev, struct vdrm_execbuf_params *p,
                     void *command, unsigned size)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);
   enum vcmd_submit_cmd2_flag flags = VCMD_SUBMIT_CMD2_FLAG_RING_IDX;

   MESA_TRACE_FUNC();

   if (p->has_in_fence_fd)
      flags |= VCMD_SUBMIT_CMD2_FLAG_IN_FENCE_FD;

   if (p->needs_out_fence_fd)
      flags |= VCMD_SUBMIT_CMD2_FLAG_OUT_FENCE_FD;

   // TODO deal with handles for implicit sync?

   simple_mtx_assert_locked(&vdev->eb_lock);

   simple_mtx_lock(&vtdev->lock);
   vpipe_submit_cmd(vtdev, command, size, flags, p->ring_idx, NULL,
                    p->num_in_syncobjs, p->num_out_syncobjs);

   if (p->num_in_syncobjs)
      vpipe_write(vtdev, p->in_syncobjs, sizeof(*p->in_syncobjs) * p->num_in_syncobjs);
   if (p->num_out_syncobjs)
      vpipe_write(vtdev, p->out_syncobjs, sizeof(*p->out_syncobjs) * p->num_out_syncobjs);

   if (p->has_in_fence_fd)
      vpipe_send_fd(vtdev, p->fence_fd);
   if (p->needs_out_fence_fd)
      p->fence_fd = vpipe_receive_fd(vtdev);

   simple_mtx_unlock(&vtdev->lock);

   return 0;
}

static int
vpipe_flush_locked(struct vdrm_device *vdev, uintptr_t *fencep)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);

   simple_mtx_assert_locked(&vdev->eb_lock);

   if (!vdev->reqbuf_len)
      return 0;

   simple_mtx_lock(&vtdev->lock);
   vpipe_submit_cmd(vtdev, vdev->reqbuf, vdev->reqbuf_len,
                    VCMD_SUBMIT_CMD2_FLAG_RING_IDX, 0,
                    fencep, 0, 0);
   simple_mtx_unlock(&vtdev->lock);

   vdev->reqbuf_len = 0;
   vdev->reqbuf_cnt = 0;

   return 0;
}

static void
vpipe_wait_fence(struct vdrm_device *vdev, uintptr_t fence)
{
   MESA_TRACE_FUNC();
   // TODO
}

/**
 * Note, does _not_ de-duplicate handles
 */
static uint32_t
vpipe_dmabuf_to_handle(struct vdrm_device *vdev, int fd)
{
   mesa_loge("%s: unimplemented", __func__);
   UNREACHABLE("unimplemented");
   return 0;
}

static uint32_t
vpipe_handle_to_res_id(struct vdrm_device *vdev, uint32_t handle)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);

   MESA_TRACE_FUNC();

   simple_mtx_lock(&vtdev->lock);
   struct vpipe_bo *vbo = handle2bo(vtdev, handle);
   uint32_t ret = vbo->res_id;
   simple_mtx_unlock(&vtdev->lock);

   return ret;
}

static uint32_t
vpipe_bo_create(struct vdrm_device *vdev, size_t size, uint32_t blob_flags,
                  uint64_t blob_id, struct vdrm_ccmd_req *req)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);
   uint32_t res_id;
   uint32_t flags = 0;
   int fd;

   MESA_TRACE_FUNC();

   if (blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE)
      flags |= VCMD_BLOB_FLAG_MAPPABLE;

   if (blob_flags & VIRTGPU_BLOB_FLAG_USE_SHAREABLE)
      flags |= VCMD_BLOB_FLAG_SHAREABLE;

   if (blob_flags & VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE)
      flags |= VCMD_BLOB_FLAG_CROSS_DEVICE;

   simple_mtx_lock(&vtdev->lock);
   if (req) {
      vpipe_submit_cmd(vtdev, req, req->len,
                       VCMD_SUBMIT_CMD2_FLAG_RING_IDX, 0,
                       NULL, 0, 0);
   }

   res_id = vpipe_create_blob(vtdev, size, flags, blob_id, &fd);
   if (!res_id)
      goto out_unlock;

   close(fd);

   uint32_t idx = util_idalloc_alloc(&vtdev->bo_idx_allocator);

   /* Ensure sufficient table size: */
   if (!util_dynarray_resize(&vtdev->bo_table, struct vpipe_bo, idx + 1)) {
      close_res_id(vtdev, res_id);
      res_id = 0;
      goto out_unlock;
   }

   struct vpipe_bo *vbo = handle2bo(vtdev, idx2handle(idx));

   vbo->res_id = res_id;

out_unlock:
   simple_mtx_unlock(&vtdev->lock);

   if (!res_id)
      return 0;

   return idx2handle(idx);
}

static int
vpipe_bo_wait(struct vdrm_device *vdev, uint32_t handle)
{
   MESA_TRACE_FUNC();
   // TODO implicit sync
   return 0;
}

static int vpipe_bo_export_dmabuf(struct vdrm_device *vdev, uint32_t handle);

static void *
vpipe_bo_map(struct vdrm_device *vdev, uint32_t handle, size_t size, void *placed_addr)
{
   MESA_TRACE_FUNC();

   int fd = vpipe_bo_export_dmabuf(vdev, handle);
   void *ret = mmap(placed_addr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   close(fd);

   return ret;
}

static int
vpipe_bo_export_dmabuf(struct vdrm_device *vdev, uint32_t handle)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_resource_export_fd[VCMD_RESOURCE_EXPORT_FD_SIZE];

   MESA_TRACE_FUNC();

   simple_mtx_lock(&vtdev->lock);
   struct vpipe_bo *vbo = handle2bo(vtdev, handle);

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_RESOURCE_EXPORT_FD_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_RESOURCE_EXPORT_FD;

   vcmd_resource_export_fd[VCMD_RESOURCE_EXPORT_FD_RES_HANDLE] = vbo->res_id;

   vpipe_write(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vtdev, vcmd_resource_export_fd, sizeof(vcmd_resource_export_fd));

   vpipe_read(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 0);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_RESOURCE_EXPORT_FD);

   int ret = vpipe_receive_fd(vtdev);
   simple_mtx_unlock(&vtdev->lock);

   return ret;
}

static void
vpipe_bo_close(struct vdrm_device *vdev, uint32_t handle)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);

   MESA_TRACE_FUNC();

   /* Flush any buffered commands first, so the detach_resource doesn't
    * overtake any buffered ccmd which references the resource:
    *
    * TODO maybe move this into core??
    */
   if (vdev->reqbuf_len) {
      simple_mtx_lock(&vdev->eb_lock);
      vpipe_flush_locked(vdev, NULL);
      simple_mtx_unlock(&vdev->eb_lock);
   }

   simple_mtx_lock(&vtdev->lock);
   struct vpipe_bo *vbo = handle2bo(vtdev, handle);

   close_res_id(vtdev, vbo->res_id);
   util_idalloc_free(&vtdev->bo_idx_allocator, handle2idx(handle));
   simple_mtx_unlock(&vtdev->lock);
}

static void
vpipe_close(struct vdrm_device *vdev)
{
   struct vpipe_device *vtdev = to_vpipe_device(vdev);

   MESA_TRACE_FUNC();

   simple_mtx_lock(&vtdev->lock);
   close_res_id(vtdev, vtdev->shmem_res_id);
   simple_mtx_unlock(&vtdev->lock);

   util_dynarray_fini(&vtdev->bo_table);
   util_idalloc_fini(&vtdev->bo_idx_allocator);

   close(vtdev->sock_fd);
}

static const struct vdrm_device_funcs funcs = {
   .flush_locked = vpipe_flush_locked,
   .wait_fence = vpipe_wait_fence,
   .execbuf_locked = vpipe_execbuf_locked,
   .dmabuf_to_handle = vpipe_dmabuf_to_handle,
   .handle_to_res_id = vpipe_handle_to_res_id,
   .bo_create = vpipe_bo_create,
   .bo_wait = vpipe_bo_wait,
   .bo_map = vpipe_bo_map,
   .bo_export_dmabuf = vpipe_bo_export_dmabuf,
   .bo_close = vpipe_bo_close,
   .close = vpipe_close,
};

static int
connect_sock(void)
{
   struct sockaddr_un un;
   int sock, ret;

   MESA_TRACE_FUNC();

   sock = socket(PF_UNIX, SOCK_STREAM, 0);
   if (sock < 0)
      return -1;

   memset(&un, 0, sizeof(un));
   un.sun_family = AF_UNIX;
   snprintf(un.sun_path, sizeof(un.sun_path), "%s", VTEST_DEFAULT_SOCKET_NAME);

   do {
      ret = 0;
      if (connect(sock, (struct sockaddr *)&un, sizeof(un)) < 0) {
         ret = -errno;
      }
   } while (ret == -EINTR);

   if (ret) {
      close(sock);
      return ret;
   }

   return sock;
}

static void
send_init(struct vpipe_device *vtdev)
{
   uint32_t buf[VTEST_HDR_SIZE];
   const char *comm = util_get_process_name();

   buf[VTEST_CMD_LEN] = strlen(comm) + 1;
   buf[VTEST_CMD_ID] = VCMD_CREATE_RENDERER;

   vpipe_write(vtdev, &buf, sizeof(buf));
   vpipe_write(vtdev, (void *)comm, strlen(comm) + 1);
}

static int
negotiate_version(struct vpipe_device *vtdev)
{
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t version_buf[VCMD_PROTOCOL_VERSION_SIZE];
   uint32_t busy_wait_buf[VCMD_BUSY_WAIT_SIZE];
   uint32_t busy_wait_result[1];
   ASSERTED int ret;

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_PING_PROTOCOL_VERSION_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_PING_PROTOCOL_VERSION;
   vpipe_write(vtdev, &vpipe_hdr, sizeof(vpipe_hdr));

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_BUSY_WAIT_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;
   busy_wait_buf[VCMD_BUSY_WAIT_HANDLE] = 0;
   busy_wait_buf[VCMD_BUSY_WAIT_FLAGS] = 0;
   vpipe_write(vtdev, &vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vtdev, &busy_wait_buf, sizeof(busy_wait_buf));

   ret = vpipe_read(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(ret);

   if (vpipe_hdr[VTEST_CMD_ID] == VCMD_PING_PROTOCOL_VERSION) {
     /* Read dummy busy_wait response */
     ret = vpipe_read(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
     assert(ret);
     ret = vpipe_read(vtdev, busy_wait_result, sizeof(busy_wait_result));
     assert(ret);

     vpipe_hdr[VTEST_CMD_LEN] = VCMD_PROTOCOL_VERSION_SIZE;
     vpipe_hdr[VTEST_CMD_ID] = VCMD_PROTOCOL_VERSION;
     version_buf[VCMD_PROTOCOL_VERSION_VERSION] = VTEST_PROTOCOL_VERSION;
     vpipe_write(vtdev, &vpipe_hdr, sizeof(vpipe_hdr));
     vpipe_write(vtdev, &version_buf, sizeof(version_buf));

     ret = vpipe_read(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
     assert(ret);
     ret = vpipe_read(vtdev, version_buf, sizeof(version_buf));
     assert(ret);
     return version_buf[VCMD_PROTOCOL_VERSION_VERSION];
   }

   /* Read dummy busy_wait response */
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_RESOURCE_BUSY_WAIT);
   ret = vpipe_read(vtdev, busy_wait_result, sizeof(busy_wait_result));
   assert(ret);

   /* Old server, return version 0 */
   return 0;
}

static bool
get_param(struct vpipe_device *vtdev, enum vcmd_param param, uint32_t *value)
{
   uint32_t vtest_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_get_param[VCMD_GET_PARAM_SIZE];
   vtest_hdr[VTEST_CMD_LEN] = VCMD_GET_PARAM_SIZE;
   vtest_hdr[VTEST_CMD_ID] = VCMD_GET_PARAM;
   vcmd_get_param[VCMD_GET_PARAM_PARAM] = param;

   vpipe_write(vtdev, vtest_hdr, sizeof(vtest_hdr));
   vpipe_write(vtdev, vcmd_get_param, sizeof(vcmd_get_param));

   vpipe_read(vtdev, vtest_hdr, sizeof(vtest_hdr));
   assert(vtest_hdr[VTEST_CMD_LEN] == 2);
   assert(vtest_hdr[VTEST_CMD_ID] == VCMD_GET_PARAM);

   uint32_t resp[2];
   vpipe_read(vtdev, resp, sizeof(resp));

   *value = resp[1];

   return !!resp[0];
}

static int
get_capset(struct vpipe_device *vtdev)
{
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_get_capset[VCMD_GET_CAPSET_SIZE];

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_GET_CAPSET_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_GET_CAPSET;

   vcmd_get_capset[VCMD_GET_CAPSET_ID] = VIRTGPU_DRM_CAPSET_DRM;
   vcmd_get_capset[VCMD_GET_CAPSET_VERSION] = 0;

   vpipe_write(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vtdev, vcmd_get_capset, sizeof(vcmd_get_capset));

   vpipe_read(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_GET_CAPSET);

   uint32_t valid;
   vpipe_read(vtdev, &valid, sizeof(valid));
   if (!valid) {
      /* unsupported id or version */
      return -EINVAL;
   }

   size_t read_size = (vpipe_hdr[VTEST_CMD_LEN] - 1) * 4;
   size_t capset_size = sizeof(vtdev->base.caps);
   void *capset = &vtdev->base.caps;
   if (capset_size >= read_size) {
      vpipe_read(vtdev, capset, read_size);
      memset(capset + read_size, 0, capset_size - read_size);
   } else {
      vpipe_read(vtdev, capset, capset_size);

      /* If the remote end sends us back a larger capset, we need to
       * swallow the extra data:
       */
      char temp[256];
      read_size -= capset_size;
      while (read_size) {
         const size_t temp_size = MIN2(read_size, ARRAY_SIZE(temp));
         vpipe_read(vtdev, temp, temp_size);
         read_size -= temp_size;
      }
   }

   return 0;
}

static void
set_context(struct vpipe_device *vtdev)
{
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_context_init[VCMD_CONTEXT_INIT_SIZE];

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_CONTEXT_INIT_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_CONTEXT_INIT;
   vcmd_context_init[VCMD_CONTEXT_INIT_CAPSET_ID] = VIRTGPU_DRM_CAPSET_DRM;

   vpipe_write(vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vtdev, vcmd_context_init, sizeof(vcmd_context_init));
}

static int
init_shmem(struct vpipe_device *vtdev)
{
   struct vdrm_device *vdev = &vtdev->base;
   uint32_t res_id;
   int shmem_fd;

   res_id = vpipe_create_blob(vtdev, SHMEM_SZ, VCMD_BLOB_FLAG_MAPPABLE,
                              0, &shmem_fd);
   if (!res_id)
      return -ENOMEM;

   vtdev->shmem_res_id = res_id;

   vdev->shmem = mmap(0, SHMEM_SZ, PROT_READ | PROT_WRITE,
                      MAP_SHARED, shmem_fd, 0);

   /* We no longer need the fd: */
   close(shmem_fd);

   if (!vdev->shmem) {
      close_res_id(vtdev, vtdev->shmem_res_id);
      vtdev->shmem_res_id = 0;
      return -ENOMEM;
   }

   uint32_t offset = vdev->shmem->rsp_mem_offset;
   vdev->rsp_mem_len = SHMEM_SZ - offset;
   vdev->rsp_mem = &((uint8_t *)vdev->shmem)[offset];

   return 0;
}

struct vdrm_device * vdrm_vpipe_connect(uint32_t context_type);

struct vdrm_device *
vdrm_vpipe_connect(uint32_t context_type)
{
   struct vpipe_device *vtdev;
   struct vdrm_device *vdev = NULL;
   int sock_fd = connect_sock();
   int ret;

   MESA_TRACE_FUNC();

   if (sock_fd < 0) {
      mesa_loge("failed to connect: %s", strerror(errno));
      return NULL;
   }

   vtdev = calloc(1, sizeof(*vtdev));

   vtdev->sock_fd = sock_fd;
   simple_mtx_init(&vtdev->lock, mtx_plain);

   util_idalloc_init(&vtdev->bo_idx_allocator, 512);
   vtdev->bo_table = UTIL_DYNARRAY_INIT;

   simple_mtx_lock(&vtdev->lock);
   send_init(vtdev);
   vtdev->protocol_version = negotiate_version(vtdev);

   /* Version 1 is deprecated. */
   if (vtdev->protocol_version == 1)
      vtdev->protocol_version = 0;

   // TODO minimum protocol version?

   mesa_logd("vpipe connected, protocol version %d", vtdev->protocol_version);

   vdev = &vtdev->base;
   vdev->funcs = &funcs;

   ret = get_capset(vtdev);
   if (ret) {
      mesa_loge("could not get caps: %s", strerror(errno));
      goto error;
   }

   if (vdev->caps.context_type != context_type) {
      mesa_loge("wrong context_type: %u", vdev->caps.context_type);
      goto error;
   }

   set_context(vtdev);
   init_shmem(vtdev);

   uint32_t value;

   /* The param will only be present if any syncobj is supported by the kernel
    * driver, but the value will only be 1 if DRM_CAP_SYNCOBJ_TIMELINE and
    * DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_TIMELINE are supported:
    */
   vtdev->has_syncobj = get_param(vtdev, VCMD_PARAM_HAS_TIMELINE_SYNCOBJ, &value);
   vtdev->has_timeline_syncobj = vtdev->has_syncobj && value;

   simple_mtx_unlock(&vtdev->lock);

   return vdev;

error:
   simple_mtx_unlock(&vtdev->lock);
   vpipe_close(vdev);
   return NULL;
}

/*
 * Sync-obj shim implementation, deals with us not having direct access to
 * syncobj related drm ioctls
 */
struct vpipe_sync_provider {
   struct util_sync_provider base;
   struct vpipe_device *vtdev;
};
DEFINE_CAST(util_sync_provider, vpipe_sync_provider)

static int
vpipe_drm_sync_create(struct util_sync_provider *p, uint32_t flags, uint32_t *handle)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_create[VCMD_DRM_SYNC_CREATE_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_CREATE_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_CREATE;

   vcmd_drm_sync_create[VCMD_DRM_SYNC_CREATE_FLAGS] = flags;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_create, sizeof(vcmd_drm_sync_create));

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 1);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_CREATE);

   vpipe_read(vp->vtdev, handle, sizeof(*handle));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_destroy(struct util_sync_provider *p, uint32_t handle)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_destroy[VCMD_DRM_SYNC_DESTROY_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_DESTROY_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_DESTROY;
   vcmd_drm_sync_destroy[VCMD_DRM_SYNC_DESTROY_HANDLE] = handle;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_destroy, sizeof(vcmd_drm_sync_destroy));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_handle_to_fd(struct util_sync_provider *p, uint32_t handle, int *out_obj_fd)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_handle_to_fd[VCMD_DRM_SYNC_HANDLE_TO_FD_SIZE];
   int fd;

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_HANDLE_TO_FD_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_HANDLE_TO_FD;
   vcmd_drm_sync_handle_to_fd[VCMD_DRM_SYNC_HANDLE_TO_FD_HANDLE] = handle;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_handle_to_fd, sizeof(vcmd_drm_sync_handle_to_fd));

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 0);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_HANDLE_TO_FD);

   fd = vpipe_receive_fd(vp->vtdev);

   simple_mtx_unlock(&vp->vtdev->lock);

   if (fd < 0)
      return fd;

   *out_obj_fd = fd;

   return 0;
}

static int
vpipe_drm_sync_fd_to_handle(struct util_sync_provider *p, int obj_fd, uint32_t *out_handle)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t handle;

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_FD_TO_HANDLE_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_FD_TO_HANDLE;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));

   vpipe_send_fd(vp->vtdev, obj_fd);

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 1);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_HANDLE_TO_FD);

   vpipe_read(vp->vtdev, &handle, sizeof(handle));

   simple_mtx_unlock(&vp->vtdev->lock);

   *out_handle = handle;

   return 0;
}

static int
vpipe_drm_sync_import_sync_file(struct util_sync_provider *p, uint32_t handle, int sync_file_fd)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_import_sync_file[VCMD_DRM_SYNC_IMPORT_SYNC_FILE_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_IMPORT_SYNC_FILE_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_IMPORT_SYNC_FILE;
   vcmd_drm_sync_import_sync_file[VCMD_DRM_SYNC_IMPORT_SYNC_FILE_HANDLE] = handle;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_import_sync_file, sizeof(vcmd_drm_sync_import_sync_file));

   vpipe_send_fd(vp->vtdev, sync_file_fd);

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_export_sync_file(struct util_sync_provider *p, uint32_t handle, int *out_sync_file_fd)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_export_sync_file[VCMD_DRM_SYNC_EXPORT_SYNC_FILE_SIZE];
   int fd;

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_EXPORT_SYNC_FILE_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_EXPORT_SYNC_FILE;
   vcmd_drm_sync_export_sync_file[VCMD_DRM_SYNC_EXPORT_SYNC_FILE_HANDLE] = handle;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_export_sync_file, sizeof(vcmd_drm_sync_export_sync_file));

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 0);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_EXPORT_SYNC_FILE);

   fd = vpipe_receive_fd(vp->vtdev);

   simple_mtx_unlock(&vp->vtdev->lock);

   if (fd < 0)
      return fd;

   *out_sync_file_fd = fd;

   return 0;
}

static int
vpipe_drm_sync_wait(struct util_sync_provider *p, uint32_t *handles, unsigned num_handles,
                int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_wait[VCMD_DRM_SYNC_WAIT_SIZE];
   int32_t ret;
   int wait_fd = -1;

   MESA_TRACE_FUNC();

   if (timeout_nsec > os_time_get_nano())
      flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FD;

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_WAIT_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_WAIT;
   vcmd_drm_sync_wait[VCMD_DRM_SYNC_WAIT_NUM_HANDLES] = num_handles;
   vcmd_drm_sync_wait[VCMD_DRM_SYNC_WAIT_TIMEOUT_LO] = (uint32_t)timeout_nsec;
   vcmd_drm_sync_wait[VCMD_DRM_SYNC_WAIT_TIMEOUT_HI] = (uint32_t)(timeout_nsec >> 32);
   vcmd_drm_sync_wait[VCMD_DRM_SYNC_WAIT_FLAGS] = flags;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_wait, sizeof(vcmd_drm_sync_wait));
   vpipe_write(vp->vtdev, handles, num_handles * sizeof(uint32_t));

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_WAIT);

   if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FD) {
      assert(vpipe_hdr[VTEST_CMD_LEN] == 0);
      wait_fd = vpipe_receive_fd(vp->vtdev);
   } else {
      assert(vpipe_hdr[VTEST_CMD_LEN] == 2);

      vpipe_read(vp->vtdev, &ret, sizeof(ret));
      if (first_signaled)
         *first_signaled = ret;
      vpipe_read(vp->vtdev, &ret, sizeof(ret));
   }

   simple_mtx_unlock(&vp->vtdev->lock);

   if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FD) {
      vpipe_read_fd(wait_fd, &ret, sizeof(ret));
      if (first_signaled)
         *first_signaled = ret;
      vpipe_read_fd(wait_fd, &ret, sizeof(ret));

      close(wait_fd);
   }

   if (ret)
      errno = -ret;

   return ret;
}

static int
vpipe_drm_sync_reset(struct util_sync_provider *p, const uint32_t *handles, uint32_t num_handles)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_reset[VCMD_DRM_SYNC_RESET_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_RESET_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_RESET;
   vcmd_drm_sync_reset[VCMD_DRM_SYNC_WAIT_NUM_HANDLES] = num_handles;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_reset, sizeof(vcmd_drm_sync_reset));
   vpipe_write(vp->vtdev, handles, num_handles * sizeof(uint32_t));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_signal(struct util_sync_provider *p, const uint32_t *handles, uint32_t num_handles)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_signal[VCMD_DRM_SYNC_SIGNAL_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_SIGNAL_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_SIGNAL;
   vcmd_drm_sync_signal[VCMD_DRM_SYNC_SIGNAL_NUM_HANDLES] = num_handles;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_signal, sizeof(vcmd_drm_sync_signal));
   vpipe_write(vp->vtdev, handles, num_handles * sizeof(uint32_t));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_timeline_signal(struct util_sync_provider *p, const uint32_t *handles,
                               uint64_t *points, uint32_t num_handles)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_timeline_signal[VCMD_DRM_SYNC_TIMELINE_SIGNAL_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_TIMELINE_SIGNAL_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_TIMELINE_SIGNAL;
   vcmd_drm_sync_timeline_signal[VCMD_DRM_SYNC_TIMELINE_SIGNAL_NUM_HANDLES] = num_handles;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_timeline_signal, sizeof(vcmd_drm_sync_timeline_signal));
   vpipe_write(vp->vtdev, points, num_handles * sizeof(uint64_t));
   vpipe_write(vp->vtdev, handles, num_handles * sizeof(uint32_t));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_timeline_wait(struct util_sync_provider *p, uint32_t *handles, uint64_t *points,
                             unsigned num_handles, int64_t timeout_nsec, unsigned flags,
                             uint32_t *first_signaled)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_timeline_wait[VCMD_DRM_SYNC_TIMELINE_WAIT_SIZE];
   int32_t ret;
   int wait_fd = -1;

   MESA_TRACE_FUNC();

   if (timeout_nsec > os_time_get_nano())
      flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FD;

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_TIMELINE_WAIT_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_TIMELINE_WAIT;
   vcmd_drm_sync_timeline_wait[VCMD_DRM_SYNC_TIMELINE_WAIT_NUM_HANDLES] = num_handles;
   vcmd_drm_sync_timeline_wait[VCMD_DRM_SYNC_TIMELINE_WAIT_TIMEOUT_LO] = (uint32_t)timeout_nsec;
   vcmd_drm_sync_timeline_wait[VCMD_DRM_SYNC_TIMELINE_WAIT_TIMEOUT_HI] = (uint32_t)(timeout_nsec >> 32);
   vcmd_drm_sync_timeline_wait[VCMD_DRM_SYNC_TIMELINE_WAIT_FLAGS] = flags;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_timeline_wait, sizeof(vcmd_drm_sync_timeline_wait));
   vpipe_write(vp->vtdev, points, num_handles * sizeof(uint64_t));
   vpipe_write(vp->vtdev, handles, num_handles * sizeof(uint32_t));

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_WAIT);

   if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FD) {
      assert(vpipe_hdr[VTEST_CMD_LEN] == 0);
      wait_fd = vpipe_receive_fd(vp->vtdev);
   } else {
      assert(vpipe_hdr[VTEST_CMD_LEN] == 2);

      vpipe_read(vp->vtdev, &ret, sizeof(ret));
      if (first_signaled)
         *first_signaled = ret;
      vpipe_read(vp->vtdev, &ret, sizeof(ret));
   }

   simple_mtx_unlock(&vp->vtdev->lock);

   if (flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FD) {
      vpipe_read_fd(wait_fd, &ret, sizeof(ret));
      if (first_signaled)
         *first_signaled = ret;
      vpipe_read_fd(wait_fd, &ret, sizeof(ret));

      close(wait_fd);
   }

   if (ret)
      errno = -ret;

   return ret;
}

static int
vpipe_drm_sync_query(struct util_sync_provider *p, uint32_t *handles, uint64_t *points,
                 uint32_t num_handles, uint32_t flags)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_query[VCMD_DRM_SYNC_QUERY_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_QUERY_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_QUERY;
   vcmd_drm_sync_query[VCMD_DRM_SYNC_QUERY_NUM_HANDLES] = num_handles;
   vcmd_drm_sync_query[VCMD_DRM_SYNC_QUERY_FLAGS] = flags;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_query, sizeof(vcmd_drm_sync_query));
   vpipe_write(vp->vtdev, handles, num_handles * sizeof(uint32_t));

   vpipe_read(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   assert(vpipe_hdr[VTEST_CMD_LEN] == 0);
   assert(vpipe_hdr[VTEST_CMD_ID] == VCMD_DRM_SYNC_QUERY);

   vpipe_read(vp->vtdev, points, num_handles * sizeof(uint64_t));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static int
vpipe_drm_sync_transfer(struct util_sync_provider *p, uint32_t dst_handle, uint64_t dst_point,
                    uint32_t src_handle, uint64_t src_point, uint32_t flags)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   uint32_t vpipe_hdr[VTEST_HDR_SIZE];
   uint32_t vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_SIZE];

   MESA_TRACE_FUNC();

   vpipe_hdr[VTEST_CMD_LEN] = VCMD_DRM_SYNC_TRANSFER_SIZE;
   vpipe_hdr[VTEST_CMD_ID] = VCMD_DRM_SYNC_TRANSFER;
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_DST_HANDLE] = dst_handle;
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_DST_POINT_LO] = (uint32_t)dst_point;
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_DST_POINT_HI] = (uint32_t)(dst_point >> 32);
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_SRC_HANDLE] = src_handle;
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_SRC_POINT_LO] = (uint32_t)src_point;
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_SRC_POINT_HI] = (uint32_t)(src_point >> 32);
   vcmd_drm_sync_transfer[VCMD_DRM_SYNC_TRANSFER_FLAGS] = flags;

   simple_mtx_lock(&vp->vtdev->lock);

   vpipe_write(vp->vtdev, vpipe_hdr, sizeof(vpipe_hdr));
   vpipe_write(vp->vtdev, vcmd_drm_sync_transfer, sizeof(vcmd_drm_sync_transfer));

   simple_mtx_unlock(&vp->vtdev->lock);

   return 0;
}

static void
vpipe_sync_finalize(struct util_sync_provider *p)
{
   free(p);
}

static struct util_sync_provider *
vpipe_sync_clone(struct util_sync_provider *p)
{
   struct vpipe_sync_provider *vp = to_vpipe_sync_provider(p);
   return vdrm_vpipe_get_sync(&vp->vtdev->base);
}

struct util_sync_provider *
vdrm_vpipe_get_sync(struct vdrm_device *vdrm)
{
   if (vdrm->funcs != &funcs)
      return NULL;

   struct vpipe_device *vtdev = to_vpipe_device(vdrm);
   struct vpipe_sync_provider *s = calloc(1, sizeof(*s));

   s->base = (struct util_sync_provider){
      .create = vpipe_drm_sync_create,
      .destroy = vpipe_drm_sync_destroy,
      .handle_to_fd = vpipe_drm_sync_handle_to_fd,
      .fd_to_handle = vpipe_drm_sync_fd_to_handle,
      .import_sync_file = vpipe_drm_sync_import_sync_file,
      .export_sync_file = vpipe_drm_sync_export_sync_file,
      .wait = vpipe_drm_sync_wait,
      .reset = vpipe_drm_sync_reset,
      .signal = vpipe_drm_sync_signal,
      .query = vpipe_drm_sync_query,
      .transfer = vpipe_drm_sync_transfer,
      .finalize = vpipe_sync_finalize,
      .clone = vpipe_sync_clone,
   };

   if (vtdev->has_timeline_syncobj) {
      s->base.timeline_signal = vpipe_drm_sync_timeline_signal;
      s->base.timeline_wait = vpipe_drm_sync_timeline_wait;
   }

   s->vtdev = to_vpipe_device(vdrm);

   return &s->base;
}
