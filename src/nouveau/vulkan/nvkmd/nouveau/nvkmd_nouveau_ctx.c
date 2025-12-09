/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvkmd_nouveau.h"

#include "nouveau_bo.h"
#include "nouveau_context.h"
#include "nouveau_device.h"
#include "vk_drm_syncobj.h"
#include "vk_log.h"

#include <xf86drm.h>

static ALWAYS_INLINE VkResult
nvkmd_nouveau_ctx_add_sync(struct nvkmd_ctx *ctx,
                           struct vk_object_base *log_obj,
                           uint32_t *nouveau_sync_count,
                           struct drm_nouveau_sync *nouveau_syncs,
                           struct vk_sync *sync,
                           uint64_t sync_value)
{
   if (unlikely(*nouveau_sync_count >= NVKMD_NOUVEAU_MAX_SYNCS)) {
      VkResult result = ctx->ops->flush(ctx, log_obj);
      if (result != VK_SUCCESS)
         return result;
   }

   struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(sync);
   assert(syncobj != NULL);

   nouveau_syncs[(*nouveau_sync_count)++] = (struct drm_nouveau_sync) {
      .flags = sync_value ? DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ :
                            DRM_NOUVEAU_SYNC_SYNCOBJ,
      .handle = syncobj->syncobj,
      .timeline_value = sync_value,
   };

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_create_exec_ctx(struct nvkmd_dev *_dev,
                              struct vk_object_base *log_obj,
                              enum nvkmd_engines engines,
                              struct nvkmd_ctx **ctx_out)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);
   int err;

   struct nvkmd_nouveau_exec_ctx *ctx = CALLOC_STRUCT(nvkmd_nouveau_exec_ctx);
   if (ctx == NULL)
      return vk_error(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY);

   ctx->base.ops = &nvkmd_nouveau_exec_ctx_ops;
   ctx->base.dev = &dev->base;
   ctx->ws_dev = dev->ws_dev;

   STATIC_ASSERT(NVKMD_ENGINE_COPY     == (int)NOUVEAU_WS_ENGINE_COPY);
   STATIC_ASSERT(NVKMD_ENGINE_2D       == (int)NOUVEAU_WS_ENGINE_2D);
   STATIC_ASSERT(NVKMD_ENGINE_3D       == (int)NOUVEAU_WS_ENGINE_3D);
   STATIC_ASSERT(NVKMD_ENGINE_M2MF     == (int)NOUVEAU_WS_ENGINE_M2MF);
   STATIC_ASSERT(NVKMD_ENGINE_COMPUTE  == (int)NOUVEAU_WS_ENGINE_COMPUTE);

   err = nouveau_ws_context_create(dev->ws_dev, (int)engines, &ctx->ws_ctx);
   if (err != 0) {
      FREE(ctx);
      if (err == -ENOSPC)
         return vk_error(log_obj, VK_ERROR_TOO_MANY_OBJECTS);
      else
         return vk_error(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   err = drmSyncobjCreate(dev->ws_dev->fd, 0, &ctx->syncobj);
   if (err < 0) {
      nouveau_ws_context_destroy(ctx->ws_ctx);
      FREE(ctx);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   ctx->max_push = MIN2(NVKMD_NOUVEAU_MAX_PUSH, dev->ws_dev->max_push);

   ctx->req = (struct drm_nouveau_exec) {
      .channel = ctx->ws_ctx->channel,
      .push_count = 0,
      .wait_count = 0,
      .sig_count = 0,
      .push_ptr = (uintptr_t)&ctx->req_push,
      .wait_ptr = (uintptr_t)&ctx->req_wait,
      .sig_ptr = (uintptr_t)&ctx->req_sig,
   };

   *ctx_out = &ctx->base;

   return VK_SUCCESS;
}

static void
nvkmd_nouveau_exec_ctx_destroy(struct nvkmd_ctx *_ctx)
{
   struct nvkmd_nouveau_exec_ctx *ctx = nvkmd_nouveau_exec_ctx(_ctx);

   ASSERTED int err = drmSyncobjDestroy(ctx->ws_dev->fd, ctx->syncobj);
   assert(err == 0);

   nouveau_ws_context_destroy(ctx->ws_ctx);
   FREE(ctx);
}

static VkResult
nvkmd_nouveau_exec_ctx_wait(struct nvkmd_ctx *_ctx,
                            struct vk_object_base *log_obj,
                            uint32_t wait_count,
                            const struct vk_sync_wait *waits)
{
   struct nvkmd_nouveau_exec_ctx *ctx = nvkmd_nouveau_exec_ctx(_ctx);

   for (uint32_t i = 0; i < wait_count; i++) {
      VkResult result = nvkmd_nouveau_ctx_add_sync(&ctx->base, log_obj,
                                                   &ctx->req.wait_count,
                                                   ctx->req_wait,
                                                   waits[i].sync,
                                                   waits[i].wait_value);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_exec_ctx_flush(struct nvkmd_ctx *_ctx,
                             struct vk_object_base *log_obj)
{
   struct nvkmd_nouveau_exec_ctx *ctx = nvkmd_nouveau_exec_ctx(_ctx);

   if (ctx->req.push_count == 0 &&
       ctx->req.wait_count == 0 &&
       ctx->req.sig_count == 0)
      return VK_SUCCESS;

   int err = drmCommandWriteRead(ctx->ws_dev->fd, DRM_NOUVEAU_EXEC,
                                 &ctx->req, sizeof(ctx->req));
   if (err) {
      VkResult result = VK_ERROR_UNKNOWN;
      if (err == -ENODEV)
         result = VK_ERROR_DEVICE_LOST;
      return vk_errorf(log_obj, result, "DRM_NOUVEAU_EXEC failed: %m");
   }

   ctx->req.push_count = 0;
   ctx->req.wait_count = 0;
   ctx->req.sig_count = 0;

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_exec_ctx_exec(struct nvkmd_ctx *_ctx,
                            struct vk_object_base *log_obj,
                            uint32_t exec_count,
                            const struct nvkmd_ctx_exec *execs)
{
   struct nvkmd_nouveau_exec_ctx *ctx = nvkmd_nouveau_exec_ctx(_ctx);

   for (uint32_t i = 0; i < exec_count; i++) {
      uint32_t incomplete_count = 0;
      for (uint32_t j = i; j < exec_count; j++) {
         if (!execs[j].incomplete)
            break;

         /* The last exec cannot be incomplete */
         assert(j < exec_count - 1);

         incomplete_count++;
      }
      assert(incomplete_count < ctx->max_push);

      if (unlikely(ctx->req.push_count + incomplete_count >= ctx->max_push)) {
         VkResult result = nvkmd_nouveau_exec_ctx_flush(&ctx->base, log_obj);
         if (result != VK_SUCCESS)
            return result;
      }

      /* This is the hardware limit on all current GPUs */
      assert((execs[i].addr % 4) == 0 && (execs[i].size_B % 4) == 0);
      assert(execs[i].size_B < (1u << 23));

      uint32_t flags = 0;
      if (execs[i].no_prefetch)
         flags |= DRM_NOUVEAU_EXEC_PUSH_NO_PREFETCH;

      ctx->req_push[ctx->req.push_count++] = (struct drm_nouveau_exec_push) {
         .va = execs[i].addr,
         .va_len = execs[i].size_B,
         .flags = flags,
      };
   }

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_exec_ctx_signal(struct nvkmd_ctx *_ctx,
                              struct vk_object_base *log_obj,
                              uint32_t signal_count,
                              const struct vk_sync_signal *signals)
{
   struct nvkmd_nouveau_exec_ctx *ctx = nvkmd_nouveau_exec_ctx(_ctx);

   for (uint32_t i = 0; i < signal_count; i++) {
      VkResult result = nvkmd_nouveau_ctx_add_sync(&ctx->base, log_obj,
                                                   &ctx->req.sig_count,
                                                   ctx->req_sig,
                                                   signals[i].sync,
                                                   signals[i].signal_value);
      if (result != VK_SUCCESS)
         return result;
   }

   return nvkmd_nouveau_exec_ctx_flush(&ctx->base, log_obj);
}

static VkResult
nvkmd_nouveau_exec_ctx_sync(struct nvkmd_ctx *_ctx,
                            struct vk_object_base *log_obj)
{
   struct nvkmd_nouveau_exec_ctx *ctx = nvkmd_nouveau_exec_ctx(_ctx);
   VkResult result;

   if (unlikely(ctx->req.sig_count >= NVKMD_NOUVEAU_MAX_SYNCS)) {
      result = nvkmd_nouveau_exec_ctx_flush(&ctx->base, log_obj);
      if (result != VK_SUCCESS)
         return result;
   }

   ctx->req_sig[ctx->req.sig_count++] = (struct drm_nouveau_sync) {
      .flags = DRM_NOUVEAU_SYNC_SYNCOBJ,
      .handle = ctx->syncobj,
   };

   result = nvkmd_nouveau_exec_ctx_flush(&ctx->base, log_obj);
   if (result != VK_SUCCESS)
      return result;

   int err = drmSyncobjWait(ctx->ws_dev->fd, &ctx->syncobj, 1,
                            INT64_MAX, 0, NULL);
   if (err) {
      return vk_errorf(log_obj, VK_ERROR_UNKNOWN,
                       "DRM_SYNCOBJ_WAIT failed: %m");
   }

   /* Push an empty again, just to check for errors */
   struct drm_nouveau_exec empty = {
      .channel = ctx->req.channel,
   };
   err = drmCommandWriteRead(ctx->ws_dev->fd, DRM_NOUVEAU_EXEC,
                             &empty, sizeof(empty));
   if (err) {
      return vk_errorf(log_obj, VK_ERROR_DEVICE_LOST,
                       "DRM_NOUVEAU_EXEC failed: %m");
   }

   return VK_SUCCESS;
}

const struct nvkmd_ctx_ops nvkmd_nouveau_exec_ctx_ops = {
   .destroy = nvkmd_nouveau_exec_ctx_destroy,
   .wait = nvkmd_nouveau_exec_ctx_wait,
   .exec = nvkmd_nouveau_exec_ctx_exec,
   .signal = nvkmd_nouveau_exec_ctx_signal,
   .flush = nvkmd_nouveau_exec_ctx_flush,
   .sync = nvkmd_nouveau_exec_ctx_sync,
};

static VkResult
nvkmd_nouveau_create_bind_ctx(struct nvkmd_dev *_dev,
                              struct vk_object_base *log_obj,
                              struct nvkmd_ctx **ctx_out)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);

   struct nvkmd_nouveau_bind_ctx *ctx = CALLOC_STRUCT(nvkmd_nouveau_bind_ctx);
   if (ctx == NULL)
      return vk_error(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY);

   ctx->base.ops = &nvkmd_nouveau_bind_ctx_ops;
   ctx->base.dev = &dev->base;
   ctx->ws_dev = dev->ws_dev;

   ctx->req = (struct drm_nouveau_vm_bind) {
      .flags = DRM_NOUVEAU_VM_BIND_RUN_ASYNC,
      .op_count = 0,
      .op_ptr = (uintptr_t)&ctx->req_ops,
      .wait_count = 0,
      .sig_count = 0,
      .wait_ptr = (uintptr_t)&ctx->req_wait,
      .sig_ptr = (uintptr_t)&ctx->req_sig,
   };

   *ctx_out = &ctx->base;

   return VK_SUCCESS;
}

static void
nvkmd_nouveau_bind_ctx_destroy(struct nvkmd_ctx *_ctx)
{
   struct nvkmd_nouveau_bind_ctx *ctx = nvkmd_nouveau_bind_ctx(_ctx);

   FREE(ctx);
}

static VkResult
nvkmd_nouveau_bind_ctx_wait(struct nvkmd_ctx *_ctx,
                            struct vk_object_base *log_obj,
                            uint32_t wait_count,
                            const struct vk_sync_wait *waits)
{
   struct nvkmd_nouveau_bind_ctx *ctx = nvkmd_nouveau_bind_ctx(_ctx);

   for (uint32_t i = 0; i < wait_count; i++) {
      VkResult result = nvkmd_nouveau_ctx_add_sync(&ctx->base, log_obj,
                                                   &ctx->req.wait_count,
                                                   ctx->req_wait,
                                                   waits[i].sync,
                                                   waits[i].wait_value);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_bind_ctx_flush(struct nvkmd_ctx *_ctx,
                             struct vk_object_base *log_obj)
{
   struct nvkmd_nouveau_bind_ctx *ctx = nvkmd_nouveau_bind_ctx(_ctx);

   if (ctx->req.op_count == 0 &&
       ctx->req.wait_count == 0 &&
       ctx->req.sig_count == 0)
      return VK_SUCCESS;

   int err = drmCommandWriteRead(ctx->ws_dev->fd, DRM_NOUVEAU_VM_BIND,
                                 &ctx->req, sizeof(ctx->req));
   if (err) {
      return vk_errorf(log_obj, VK_ERROR_UNKNOWN,
                       "DRM_NOUVEAU_VM_BIND failed: %m");
   }

   ctx->req.op_count = 0;
   ctx->req.wait_count = 0;
   ctx->req.sig_count = 0;

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_bind_ctx_bind(struct nvkmd_ctx *_ctx,
                            struct vk_object_base *log_obj,
                            uint32_t bind_count,
                            const struct nvkmd_ctx_bind *binds)
{
   struct nvkmd_nouveau_bind_ctx *ctx = nvkmd_nouveau_bind_ctx(_ctx);

   for (uint32_t i = 0; i < bind_count; i++) {
      STATIC_ASSERT(NVKMD_BIND_OP_BIND   == DRM_NOUVEAU_VM_BIND_OP_MAP);
      STATIC_ASSERT(NVKMD_BIND_OP_UNBIND == DRM_NOUVEAU_VM_BIND_OP_UNMAP);

      struct drm_nouveau_vm_bind_op op = {
         .op = binds[i].op,
         .addr = binds[i].va->addr + binds[i].va_offset_B,
         .range = binds[i].range_B,
         .flags = binds[i].va->pte_kind,
      };

      if (binds[i].op == NVKMD_BIND_OP_BIND) {
         op.handle = nvkmd_nouveau_mem(binds[i].mem)->bo->handle;
         op.bo_offset = binds[i].mem_offset_B;
      }

      if (ctx->req.op_count > 0) {
         struct drm_nouveau_vm_bind_op *prev_op =
            &ctx->req_ops[ctx->req.op_count - 1];

         /* Try to coalesce bind ops together if we can */
         if (op.op == prev_op->op &&
             op.flags == prev_op->flags &&
             op.handle == prev_op->handle &&
             op.addr == prev_op->addr + prev_op->range &&
             op.bo_offset == prev_op->bo_offset + prev_op->range) {
            prev_op->range += op.range;
            continue;
         }
      }

      if (unlikely(ctx->req.op_count >= NVKMD_NOUVEAU_MAX_BINDS)) {
         VkResult result = nvkmd_nouveau_bind_ctx_flush(&ctx->base, log_obj);
         if (result != VK_SUCCESS)
            return result;
      }

      ctx->req_ops[ctx->req.op_count++] = op;
   }

   return VK_SUCCESS;
}

static VkResult
nvkmd_nouveau_bind_ctx_signal(struct nvkmd_ctx *_ctx,
                              struct vk_object_base *log_obj,
                              uint32_t signal_count,
                              const struct vk_sync_signal *signals)
{
   struct nvkmd_nouveau_bind_ctx *ctx = nvkmd_nouveau_bind_ctx(_ctx);

   for (uint32_t i = 0; i < signal_count; i++) {
      VkResult result = nvkmd_nouveau_ctx_add_sync(&ctx->base, log_obj,
                                                   &ctx->req.sig_count,
                                                   ctx->req_sig,
                                                   signals[i].sync,
                                                   signals[i].signal_value);
      if (result != VK_SUCCESS)
         return result;
   }

   return nvkmd_nouveau_bind_ctx_flush(&ctx->base, log_obj);
}

const struct nvkmd_ctx_ops nvkmd_nouveau_bind_ctx_ops = {
   .destroy = nvkmd_nouveau_bind_ctx_destroy,
   .wait = nvkmd_nouveau_bind_ctx_wait,
   .bind = nvkmd_nouveau_bind_ctx_bind,
   .signal = nvkmd_nouveau_bind_ctx_signal,
   .flush = nvkmd_nouveau_bind_ctx_flush,
};

VkResult
nvkmd_nouveau_create_ctx(struct nvkmd_dev *dev,
                         struct vk_object_base *log_obj,
                         enum nvkmd_engines engines,
                         struct nvkmd_ctx **ctx_out)
{
   if (engines == NVKMD_ENGINE_BIND) {
      return nvkmd_nouveau_create_bind_ctx(dev, log_obj, ctx_out);
   } else {
      assert(!(engines & NVKMD_ENGINE_BIND));
      return nvkmd_nouveau_create_exec_ctx(dev, log_obj, engines, ctx_out);
   }
}
