/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <xf86drm.h>
#include <libsync.h>

#include <dlfcn.h>
#include <libdrm/amdgpu.h>

#include "amdgpu_virtio_private.h"

#include "util/log.h"
#include "util/u_math.h"

int
amdvgpu_query_info(amdvgpu_device_handle dev, struct drm_amdgpu_info *info)
{
   unsigned req_len = sizeof(struct amdgpu_ccmd_query_info_req);
   unsigned rsp_len = sizeof(struct amdgpu_ccmd_query_info_rsp) + info->return_size;

   uint8_t buf[req_len];
   struct amdgpu_ccmd_query_info_req *req = (void *)buf;
   struct amdgpu_ccmd_query_info_rsp *rsp;
   assert(0 == (offsetof(struct amdgpu_ccmd_query_info_rsp, payload) % 8));

   req->hdr = AMDGPU_CCMD(QUERY_INFO, req_len);
   memcpy(&req->info, info, sizeof(struct drm_amdgpu_info));

   rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, rsp_len);

   int r = vdrm_send_req_wrapper(dev, &req->hdr, &rsp->hdr, true);
   if (r)
      return r;

   memcpy((void*)(uintptr_t)info->return_pointer, rsp->payload, info->return_size);

   return 0;
}

int
amdvgpu_query_sw_info(amdvgpu_device_handle dev, enum amdgpu_sw_info info, void *value)
{
   if (info != amdgpu_sw_info_address32_hi)
      return -EINVAL;
   memcpy(value, &dev->vdev->caps.u.amdgpu.address32_hi, 4);
   return 0;
}

int
amdvgpu_bo_set_metadata(amdvgpu_device_handle dev, uint32_t res_id,
                        struct amdgpu_bo_metadata *info)
{
   unsigned req_len = align(sizeof(struct amdgpu_ccmd_set_metadata_req) + info->size_metadata, 8);
   unsigned rsp_len = sizeof(struct amdgpu_ccmd_rsp);

   uint8_t buf[req_len];
   struct amdgpu_ccmd_set_metadata_req *req = (void *)buf;
   struct amdgpu_ccmd_rsp *rsp;

   req->hdr = AMDGPU_CCMD(SET_METADATA, req_len);
   req->res_id = res_id;
   req->flags = info->flags;
   req->tiling_info = info->tiling_info;
   req->size_metadata = info->size_metadata;
   memcpy(req->umd_metadata, info->umd_metadata, info->size_metadata);

   rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, rsp_len);
   return vdrm_send_req_wrapper(dev, &req->hdr, rsp, true);
}

int amdvgpu_bo_query_info(amdvgpu_device_handle dev, uint32_t res_id, struct amdgpu_bo_info *info) {
   unsigned req_len = sizeof(struct amdgpu_ccmd_bo_query_info_req);
   unsigned rsp_len = sizeof(struct amdgpu_ccmd_bo_query_info_rsp);

   uint8_t buf[req_len];
   struct amdgpu_ccmd_bo_query_info_req *req = (void *)buf;
   struct amdgpu_ccmd_bo_query_info_rsp *rsp;

   req->hdr = AMDGPU_CCMD(BO_QUERY_INFO, req_len);
   req->res_id = res_id;
   req->pad = 0;

   rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, rsp_len);

   int r = vdrm_send_req_wrapper(dev, &req->hdr, &rsp->hdr, true);
   if (r)
      return r;

   info->alloc_size = rsp->info.alloc_size;
   info->phys_alignment = rsp->info.phys_alignment;
   info->preferred_heap = rsp->info.preferred_heap;
   info->alloc_flags = rsp->info.alloc_flags;

   info->metadata.flags = rsp->info.metadata.flags;
   info->metadata.tiling_info = rsp->info.metadata.tiling_info;
   info->metadata.size_metadata = rsp->info.metadata.size_metadata;
   memcpy(info->metadata.umd_metadata, rsp->info.metadata.umd_metadata,
          MIN2(sizeof(info->metadata.umd_metadata), rsp->info.metadata.size_metadata));

   return 0;
}

int amdvgpu_cs_ctx_create2(amdvgpu_device_handle dev, int32_t priority,
                           uint32_t *ctx_virtio) {
   simple_mtx_lock(&dev->contexts_mutex);
   if (!dev->allow_multiple_amdgpu_ctx && _mesa_hash_table_num_entries(&dev->contexts)) {
      assert(_mesa_hash_table_num_entries(&dev->contexts) == 1);
      struct hash_entry *he = _mesa_hash_table_random_entry(&dev->contexts, NULL);
      struct amdvgpu_context *ctx = he->data;
      p_atomic_inc(&ctx->refcount);
      *ctx_virtio = (uint32_t)(uintptr_t)he->key;
      simple_mtx_unlock(&dev->contexts_mutex);
      return 0;
   }

   struct amdgpu_ccmd_create_ctx_req req = {
      .priority = priority,
      .flags = 0,
   };
   struct amdgpu_ccmd_create_ctx_rsp *rsp;

   req.hdr = AMDGPU_CCMD(CREATE_CTX, sizeof(req));

   rsp = vdrm_alloc_rsp(dev->vdev, &req.hdr, sizeof(struct amdgpu_ccmd_create_ctx_rsp));
   int r = vdrm_send_req_wrapper(dev, &req.hdr, &rsp->hdr, true);

   if (r)
      goto unlock;

   if (rsp->ctx_id == 0) {
      r = -ENOTSUP;
      goto unlock;
   }

   struct amdvgpu_context *ctx = calloc(1, sizeof(struct amdvgpu_context) + dev->num_virtio_rings * sizeof(uint64_t));
   if (ctx == NULL) {
      r = -ENOMEM;
      goto unlock;
   }

   p_atomic_inc(&ctx->refcount);
   ctx->host_context_id = rsp->ctx_id;
   for (int i = 0; i < dev->num_virtio_rings; i++)
      ctx->ring_next_seqno[i] = 1;
   *ctx_virtio = ctx->host_context_id;

   _mesa_hash_table_insert(&dev->contexts, (void*)(uintptr_t)ctx->host_context_id, ctx);

unlock:
   simple_mtx_unlock(&dev->contexts_mutex);

   return r;
}

int amdvgpu_cs_ctx_free(amdvgpu_device_handle dev, uint32_t ctx_id)
{
   struct hash_entry *he = _mesa_hash_table_search(&dev->contexts,
                                                   (void*)(uintptr_t)ctx_id);

   if (!he)
      return -1;

   if (!dev->allow_multiple_amdgpu_ctx) {
      struct amdvgpu_context *ctx = he->data;
      if (p_atomic_dec_return(&ctx->refcount))
         return 0;
   }

   struct amdgpu_ccmd_create_ctx_req req = {
      .id = ctx_id,
      .flags = AMDGPU_CCMD_CREATE_CTX_DESTROY,
   };
   req.hdr = AMDGPU_CCMD(CREATE_CTX, sizeof(req));

   _mesa_hash_table_remove(&dev->contexts, he);

   free(he->data);

   struct amdgpu_ccmd_create_ctx_rsp *rsp;
   rsp = vdrm_alloc_rsp(dev->vdev, &req.hdr, sizeof(struct amdgpu_ccmd_create_ctx_rsp));

   return vdrm_send_req_wrapper(dev, &req.hdr, &rsp->hdr, false);
}

int
amdvgpu_device_get_fd(amdvgpu_device_handle dev) {
   return dev->fd;
}

const char *
amdvgpu_get_marketing_name(amdvgpu_device_handle dev) {
   return dev->vdev->caps.u.amdgpu.marketing_name;
}

static uint32_t cs_chunk_ib_to_virtio_ring_idx(amdvgpu_device_handle dev,
                                               struct drm_amdgpu_cs_chunk_ib *ib) {
   assert(dev->virtio_ring_mapping[ib->ip_type] != 0);
   return dev->virtio_ring_mapping[ib->ip_type] + ib->ring;
}

int
amdvgpu_cs_submit_raw2(amdvgpu_device_handle dev, uint32_t ctx_id,
                       uint32_t bo_list_handle,
                       int num_chunks, struct drm_amdgpu_cs_chunk *chunks,
                       uint64_t *seqno)
{
   unsigned rsp_len = sizeof(struct amdgpu_ccmd_rsp);

   struct extra_data_info {
      const void *ptr;
      uint32_t size;
   } extra[1 + num_chunks];

   int chunk_count = 0;
   unsigned offset = 0;

   struct desc {
      uint16_t chunk_id;
      uint16_t length_dw;
      uint32_t offset;
   };
   struct desc descriptors[num_chunks];

   unsigned virtio_ring_idx = 0xffffffff;

   uint32_t syncobj_in_count = 0, syncobj_out_count = 0;
   struct drm_virtgpu_execbuffer_syncobj *syncobj_in = NULL;
   struct drm_virtgpu_execbuffer_syncobj *syncobj_out = NULL;
   uint8_t *buf = NULL;
   int ret;

   const bool sync_submit = dev->sync_cmd & (1u << AMDGPU_CCMD_CS_SUBMIT);

   struct hash_entry *he = _mesa_hash_table_search(&dev->contexts, (void*)(uintptr_t)ctx_id);
   if (!he)
      return -1;

   struct amdvgpu_context *vctx = he->data;

   /* Extract pointers from each chunk and copy them to the payload. */
   for (int i = 0; i < num_chunks; i++) {
      int extra_idx = 1 + chunk_count;
      const uint32_t cid = chunks[i].chunk_id;
      if (cid == AMDGPU_CHUNK_ID_BO_HANDLES) {
         struct drm_amdgpu_bo_list_in *list_in = (void*) (uintptr_t)chunks[i].chunk_data;
         extra[extra_idx].ptr = (void*) (uintptr_t)list_in->bo_info_ptr;
         extra[extra_idx].size = list_in->bo_info_size * list_in->bo_number;
      } else if (cid == AMDGPU_CHUNK_ID_DEPENDENCIES ||
                 cid == AMDGPU_CHUNK_ID_FENCE ||
                 cid == AMDGPU_CHUNK_ID_IB) {
         extra[extra_idx].ptr = (void*)(uintptr_t)chunks[i].chunk_data;
         extra[extra_idx].size = chunks[i].length_dw * 4;

         if (cid == AMDGPU_CHUNK_ID_IB) {
            struct drm_amdgpu_cs_chunk_ib *ib = (void*)(uintptr_t)chunks[i].chunk_data;
            virtio_ring_idx = cs_chunk_ib_to_virtio_ring_idx(dev, ib);
         }
      } else if (cid == AMDGPU_CHUNK_ID_SYNCOBJ_OUT ||
                 cid == AMDGPU_CHUNK_ID_SYNCOBJ_IN ||
                 cid == AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT ||
                 cid == AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL) {
         /* Translate from amdgpu CHUNK_ID_SYNCOBJ_* to drm_virtgpu_execbuffer_syncobj */
         struct drm_amdgpu_cs_chunk_sem *amd_sem = NULL;
         struct drm_amdgpu_cs_chunk_syncobj *amd_syncobj = NULL;
         unsigned new_syncobj_count;
         struct drm_virtgpu_execbuffer_syncobj **syncobjs;
         uint32_t *count;

         switch (cid) {
            case AMDGPU_CHUNK_ID_SYNCOBJ_OUT:
               syncobjs = &syncobj_out;
               count = &syncobj_out_count;
               new_syncobj_count = (chunks[i].length_dw * 4) / sizeof(*amd_sem);
               amd_sem = (void*) (uintptr_t)chunks[i].chunk_data;
               break;
            case AMDGPU_CHUNK_ID_SYNCOBJ_IN:
               syncobjs = &syncobj_in;
               count = &syncobj_in_count;
               new_syncobj_count = (chunks[i].length_dw * 4) / sizeof(*amd_sem);
               amd_sem = (void*) (uintptr_t)chunks[i].chunk_data;
               break;
            case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL:
               syncobjs = &syncobj_out;
               count = &syncobj_out_count;
               new_syncobj_count = (chunks[i].length_dw * 4) / sizeof(*amd_syncobj);
               amd_syncobj = (void*) (uintptr_t)chunks[i].chunk_data;
               break;
            case AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT:
               syncobjs = &syncobj_in;
               count = &syncobj_in_count;
               new_syncobj_count = (chunks[i].length_dw * 4) / sizeof(*amd_syncobj);
               amd_syncobj = (void*) (uintptr_t)chunks[i].chunk_data;
               break;
            default:
               assert(false);
               ret = -EINVAL;
               goto error;
         }


         *syncobjs = realloc(*syncobjs, (*count + new_syncobj_count) * sizeof(struct drm_virtgpu_execbuffer_syncobj));
         if (*syncobjs == NULL) {
            ret = -ENOMEM;
            goto error;
         }

         int start = *count;
         for (int j = 0; j < new_syncobj_count; j++) {
            if (amd_syncobj) {
               (*syncobjs)[start + j].handle = amd_syncobj[j].handle;
               /* radv uses DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT but vk_queue already used
                * WAIT_AVAILABLE, so clear the flag. This is not 100% correct as between
                * the wait and the submit the syncobj fence might get changed but since
                * virtgpu doesn't support WAIT_FOR_SUBMIT yet, this is the best we can do.
                */
               (*syncobjs)[start + j].flags = 0;
               (*syncobjs)[start + j].point = amd_syncobj[j].point;
            } else {
               (*syncobjs)[start + j].handle = amd_sem[j].handle;
               (*syncobjs)[start + j].flags = 0;
               (*syncobjs)[start + j].point = 0;
            }
         }
         *count += new_syncobj_count;

         /* This chunk was converted to virtgpu UAPI so we don't need to forward it
          * to the host.
          */
         continue;
      } else {
         mesa_loge("Unhandled chunk_id: %d\n", cid);
         continue;
      }
      descriptors[chunk_count].chunk_id = cid;
      descriptors[chunk_count].offset = offset;
      descriptors[chunk_count].length_dw = extra[extra_idx].size / 4;
      offset += extra[extra_idx].size;
      chunk_count++;
   }
   assert(virtio_ring_idx != 0xffffffff);

   /* Copy the descriptors at the beginning. */
   extra[0].ptr = descriptors;
   extra[0].size = chunk_count * sizeof(struct desc);

   /* Determine how much extra space we need. */
   uint32_t req_len = sizeof(struct amdgpu_ccmd_cs_submit_req);
   uint32_t e_offset = req_len;
   for (unsigned i = 0; i < 1 + chunk_count; i++)
      req_len += extra[i].size;

   /* Allocate the command buffer. */
   buf = malloc(req_len);
   if (buf == NULL) {
      ret = -ENOMEM;
      goto error;
   }
   struct amdgpu_ccmd_cs_submit_req *req = (void*)buf;
   req->hdr = AMDGPU_CCMD(CS_SUBMIT, req_len);
   req->ctx_id = ctx_id;
   req->num_chunks = chunk_count;
   req->ring_idx = virtio_ring_idx;
   req->pad = 0;

   UNUSED struct amdgpu_ccmd_rsp *rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, rsp_len);

   /* Copy varying data after the fixed part of cs_submit_req. */
   for (unsigned i = 0; i < 1 + chunk_count; i++) {
      if (extra[i].size) {
         memcpy(&buf[e_offset], extra[i].ptr, extra[i].size);
         e_offset += extra[i].size;
      }
   }

   /* Optional fence out (if we want synchronous submits). */
   int *fence_fd_ptr = NULL;

   struct vdrm_execbuf_params vdrm_execbuf_p = {
      .ring_idx = virtio_ring_idx,
      .req = &req->hdr,
      .handles = NULL,
      .num_handles = 0,
      .in_syncobjs = syncobj_in,
      .out_syncobjs = syncobj_out,
      .has_in_fence_fd = 0,
      .needs_out_fence_fd = sync_submit,
      .fence_fd = 0,
      .num_in_syncobjs = syncobj_in_count,
      .num_out_syncobjs = syncobj_out_count,
   };

   if (sync_submit)
      fence_fd_ptr = &vdrm_execbuf_p.fence_fd;

   /* Push job to the host. */
   ret = vdrm_execbuf(dev->vdev, &vdrm_execbuf_p);

   /* Determine the host seqno for this job. */
   *seqno = vctx->ring_next_seqno[virtio_ring_idx - 1]++;

   if (ret == 0 && fence_fd_ptr) {
      /* Sync execution */
      sync_wait(*fence_fd_ptr, -1);
      close(*fence_fd_ptr);
      vdrm_host_sync(dev->vdev, &req->hdr);
   }

error:
   free(buf);
   free(syncobj_in);
   free(syncobj_out);

   return ret;
}

int amdvgpu_cs_query_reset_state2(amdvgpu_device_handle dev, uint32_t ctx_id,
                                  uint64_t *flags)
{
   *flags = 0;

   if (to_amdvgpu_shmem(dev->vdev->shmem)->async_error > 0)
      *flags = AMDGPU_CTX_QUERY2_FLAGS_RESET | AMDGPU_CTX_QUERY2_FLAGS_VRAMLOST;

   return 0;
}

int amdvgpu_cs_query_fence_status(amdvgpu_device_handle dev,
                                  uint32_t ctx_id,
                                  uint32_t ip_type,
                                  uint32_t ip_instance, uint32_t ring,
                                  uint64_t fence_seq_no,
                                  uint64_t timeout_ns, uint64_t flags,
                                  uint32_t *expired)
{
   unsigned req_len = sizeof(struct amdgpu_ccmd_cs_query_fence_status_req);
   unsigned rsp_len = sizeof(struct amdgpu_ccmd_cs_query_fence_status_rsp);

   uint8_t buf[req_len];
   struct amdgpu_ccmd_cs_query_fence_status_req *req = (void *)buf;
   struct amdgpu_ccmd_cs_query_fence_status_rsp *rsp;

   req->hdr = AMDGPU_CCMD(CS_QUERY_FENCE_STATUS, req_len);
   req->ctx_id = ctx_id;
   req->ip_type = ip_type;
   req->ip_instance = ip_instance;
   req->ring = ring;
   req->fence = fence_seq_no;
   req->timeout_ns = timeout_ns;
   req->flags = flags;

   rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, rsp_len);

   int r = vdrm_send_req_wrapper(dev, &req->hdr, &rsp->hdr, true);

   if (r == 0)
      *expired = rsp->expired;

   return r;
}

int amdvgpu_vm_reserve_vmid(amdvgpu_device_handle dev, int reserve) {
   unsigned req_len = sizeof(struct amdgpu_ccmd_reserve_vmid_req);

   uint8_t buf[req_len];
   struct amdgpu_ccmd_reserve_vmid_req *req = (void *)buf;
   struct amdgpu_ccmd_rsp *rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, sizeof(struct amdgpu_ccmd_rsp));

   req->hdr = AMDGPU_CCMD(RESERVE_VMID, req_len);
   req->flags = reserve ? 0 : AMDGPU_CCMD_RESERVE_VMID_UNRESERVE;

   return vdrm_send_req_wrapper(dev, &req->hdr, rsp, true);
}

int amdvgpu_cs_ctx_stable_pstate(amdvgpu_device_handle dev,
                                 uint32_t ctx_id,
                                 uint32_t op,
                                 uint32_t flags,
                                 uint32_t *out_flags) {
   unsigned req_len = sizeof(struct amdgpu_ccmd_set_pstate_req);
   unsigned rsp_len = sizeof(struct amdgpu_ccmd_set_pstate_rsp);

   uint8_t buf[req_len];
   struct amdgpu_ccmd_set_pstate_req *req = (void *)buf;
   struct amdgpu_ccmd_set_pstate_rsp *rsp;

   req->hdr = AMDGPU_CCMD(SET_PSTATE, req_len);
   req->ctx_id = ctx_id;
   req->op = op;
   req->flags = flags;
   req->pad = 0;

   rsp = vdrm_alloc_rsp(dev->vdev, &req->hdr, rsp_len);

   int r = vdrm_send_req_wrapper(dev, &req->hdr, &rsp->hdr, out_flags);

   if (r == 0 && out_flags)
      *out_flags = rsp->out_flags;

   return r;
}

int
amdvgpu_va_range_alloc(amdvgpu_device_handle dev,
                       enum amdgpu_gpu_va_range va_range_type,
                       uint64_t size,
                       uint64_t va_base_alignment,
                       uint64_t va_base_required,
                       uint64_t *va_base_allocated,
                       amdgpu_va_handle *va_range_handle,
                       uint64_t flags)
{
   return amdgpu_va_range_alloc2(dev->va_mgr, va_range_type, size,
                                 va_base_alignment, va_base_required,
                                 va_base_allocated, va_range_handle,
                                 flags);
}

bool amdvgpu_has_vm_always_valid(amdvgpu_device_handle dev)
{
   return dev->vdev->caps.u.amdgpu.has_vm_always_valid;
}