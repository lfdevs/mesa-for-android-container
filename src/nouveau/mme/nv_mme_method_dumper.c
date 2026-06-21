/*
 * Copyright © 2026 Valve Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "util/u_memory.h"

#include "drm-uapi/nouveau_drm.h"

#include "nouveau/mme/mme_builder.h"
#include "nouveau/mme/mme_value.h"

#include "nouveau_bo.h"
#include "nouveau_context.h"
#include "nouveau_device.h"
#include "nv_push.h"

#include "clce97.h"
#include "nv_push_class_dump.h"

#define _64K (1 << 16)
#define _2M  (1 << 21)

#define PUSH_BUFFER_ADDR 0x3ffcd00000
#define DATA_ADDR        0x3ffdd00000

static struct nouveau_ws_device *
find_device(int16_t min_cls, uint16_t max_cls)
{
   struct nouveau_ws_device *dev;
   drmDevicePtr devices[8];
   int max_devices = drmGetDevices2(0, devices, 8);

   int i;
   for (i = 0; i < max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == 0x10de) {
         dev = nouveau_ws_device_new(devices[i]);
         if (dev == NULL)
            continue;

         if (dev->info.cls_eng3d < min_cls || dev->info.cls_eng3d > max_cls) {
            nouveau_ws_device_destroy(dev);
            dev = NULL;
            continue;
         }

         /* Found a device */
         break;
      }
   }

   return dev;
}

struct buffer_object {
   struct nouveau_ws_bo *bo;
   void *cpu_map;
   uint64_t gpu_addr;
};

static struct buffer_object *
create_buffer_object(struct nouveau_ws_device *dev, uint64_t gpu_addr,
                     uint64_t size, uint64_t align,
                     enum nouveau_ws_bo_flags bo_flags,
                     enum nouveau_ws_bo_map_flags map_flags)
{
   struct buffer_object *obj = CALLOC_STRUCT(buffer_object);

   if (obj == NULL)
      return NULL;

   obj->bo = nouveau_ws_bo_new_mapped(dev, size, align, bo_flags, map_flags,
                                      &obj->cpu_map);

   if (obj->bo == NULL) {
      FREE(obj);
      return NULL;
   }

   nouveau_ws_bo_bind_vma(dev, obj->bo, gpu_addr, obj->bo->size, 0, 0);
   obj->gpu_addr = gpu_addr;

   return obj;
}

static void
destroy_buffer_object(struct nouveau_ws_device *dev, struct buffer_object *obj)
{
   nouveau_ws_bo_unbind_vma(dev, obj->gpu_addr, obj->bo->size);
   nouveau_ws_bo_unmap(obj->bo, obj->cpu_map);
   nouveau_ws_bo_destroy(obj->bo);
   FREE(obj);
}

inline uint32_t
high32(uint64_t x)
{
   return (uint32_t)(x >> 32);
}

inline uint32_t
low32(uint64_t x)
{
   return (uint32_t)x;
}

static inline void
mme_store_imm_addr(struct mme_builder *b, uint64_t addr, struct mme_value v)
{
   mme_mthd(b, NV9097_SET_REPORT_SEMAPHORE_A);
   mme_emit(b, mme_imm(high32(addr)));
   mme_emit(b, mme_imm(low32(addr)));
   mme_emit(b, v);
   mme_emit(b, mme_imm(0x10000000));
}

static int
nv_mme_method_dump(struct nouveau_ws_device *dev,
                   struct nouveau_ws_context *ctx)
{
   int ret;

   uint32_t syncobj_handle;
   ret = drmSyncobjCreate(dev->fd, 0, &syncobj_handle);

   if (ret != 0)
      return ret;

   struct buffer_object *pushbuf_bo = create_buffer_object(
      dev, PUSH_BUFFER_ADDR, _2M, 0, NOUVEAU_WS_BO_GART | NOUVEAU_WS_BO_MAP,
      NOUVEAU_WS_BO_WR);

   if (pushbuf_bo == NULL) {
      ret = -ENOMEM;
      goto err_create_pushbuf_bo;
   }

   struct buffer_object *data_bo = create_buffer_object(
      dev, DATA_ADDR, _2M, 0, NOUVEAU_WS_BO_GART | NOUVEAU_WS_BO_MAP,
      NOUVEAU_WS_BO_RDWR);

   if (data_bo == NULL) {
      ret = -ENOMEM;
      goto err_create_data_bo;
   }

   memset(data_bo->cpu_map, 0xDE, data_bo->bo->size);

   /* Build the macro we are going to use to do the dumping */
   struct mme_builder b;
   mme_builder_init(&b, &dev->info);
   // struct mme_value64 addr = mme_mov64(&b, mme_imm64(DATA_ADDR));
   struct mme_value64 addr = mme_load_addr64(&b);
   struct mme_value idx = mme_load(&b);
   struct mme_value count = mme_load(&b);

   mme_loop(&b, count)
   {
      struct mme_value v = mme_state_arr(&b, 0, idx);

      mme_mthd(&b, NV9097_SET_REPORT_SEMAPHORE_A);
      mme_emit_addr64(&b, addr);
      mme_emit(&b, v);
      mme_emit(&b, mme_imm(0x10000000));

      mme_add_to(&b, idx, idx, mme_imm(1));
      mme_add64_to(&b, addr, addr, mme_imm64(4));
   }
   mme_free_reg(&b, idx);
   mme_free_reg(&b, count);

   size_t macro_size = 0;
   uint32_t *macro = mme_builder_finish(&b, &macro_size);
   uint32_t macro_dw_size = macro_size / sizeof(uint32_t);

   struct nv_push push;
   struct nv_push *p = &push;

   nv_push_init(&push, pushbuf_bo->cpu_map,
                pushbuf_bo->bo->size / sizeof(uint32_t),
                BITFIELD_BIT(SUBC_NV9097));

   P_MTHD(p, NV9097, SET_OBJECT);
   P_NV9097_SET_OBJECT(p, {.class_id = dev->info.cls_eng3d, .engine_id = 0});

   P_MTHD(p, NV9097, LOAD_MME_START_ADDRESS_RAM_POINTER);
   P_NV9097_LOAD_MME_START_ADDRESS_RAM_POINTER(p, 0);
   P_NV9097_LOAD_MME_START_ADDRESS_RAM(p, 0);

   P_1INC(p, NV9097, LOAD_MME_INSTRUCTION_RAM_POINTER);
   P_NV9097_LOAD_MME_INSTRUCTION_RAM_POINTER(p, 0);
   P_INLINE_ARRAY(p, macro, macro_dw_size);
   free(macro);

   const uint32_t start_idx = NV9097_NO_OPERATION / sizeof(uint32_t);
   const uint32_t end_idx = NV9097_SET_MME_SHADOW_SCRATCH(0) / sizeof(uint32_t);
   const uint32_t max_count = end_idx - start_idx;
   const uint64_t dump_addr = data_bo->gpu_addr + start_idx * sizeof(uint32_t);

   P_1INC(p, NV9097, CALL_MME_MACRO(0));
   P_INLINE_DATA(p, dump_addr >> 32);
   P_INLINE_DATA(p, dump_addr);
   P_INLINE_DATA(p, start_idx);
   P_INLINE_DATA(p, max_count);

   struct drm_nouveau_sync sync = {
      .flags = DRM_NOUVEAU_SYNC_SYNCOBJ,
      .handle = syncobj_handle,
      .timeline_value = 0,
   };

   struct drm_nouveau_exec_push exec_push = {
      .va = pushbuf_bo->gpu_addr,
      .va_len = (uint32_t)nv_push_dw_count(p) * sizeof(uint32_t),
   };

   struct drm_nouveau_exec req = {
      .channel = ctx->channel,
      .push_count = 1,
      .sig_count = 1,
      .sig_ptr = (uintptr_t)&sync,
      .push_ptr = (uintptr_t)&exec_push,
   };

   ret = drmCommandWriteRead(dev->fd, DRM_NOUVEAU_EXEC, &req, sizeof(req));

   if (ret != 0)
      goto err_exec;

   ret = drmSyncobjWait(dev->fd, &syncobj_handle, 1, INT64_MAX,
                        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);

   if (ret != 0)
      goto err_exec;

   const uint32_t *data_map = data_bo->cpu_map;
   /* If we are here, we got a dump! Let's print the result */
   for (uint32_t i = start_idx; i < (start_idx + max_count); i++) {
      if (data_map[i] == 0)
         continue;

      const uint32_t mthd = i * sizeof(uint32_t);
      const char *mthd_name = P_PARSE_NV_MTHD(dev->info.cls_eng3d, mthd);
      fprintf(stdout, "\tmthd %04x %s\n", mthd, mthd_name);
      P_DUMP_NV_MTHD_DATA(stdout, dev->info.cls_eng3d, mthd, data_map[i],
                          "\t\t");
   }

err_exec:
   destroy_buffer_object(dev, data_bo);
err_create_data_bo:
   destroy_buffer_object(dev, pushbuf_bo);
err_create_pushbuf_bo:
   drmSyncobjDestroy(dev->fd, syncobj_handle);
   return ret;
}

int
main(int argc, char **argv)
{
   struct nouveau_ws_device *dev = find_device(FERMI_A, BLACKWELL_B);

   if (dev == NULL) {
      fprintf(stderr, "couldn't find any nouveau device\n");
      return 1;
   }

   struct nouveau_ws_context *ctx;
   int ret = nouveau_ws_context_create(dev, NOUVEAU_WS_ENGINE_3D, &ctx);
   if (ret != 0) {
      fprintf(stderr, "couldn't create context %d\n", ret);
      nouveau_ws_device_destroy(dev);
      return 1;
   }

   ret = nv_mme_method_dump(dev, ctx);
   if (ret != 0)
      fprintf(stderr, "nv_mme_method_dump: failed: %d\n", ret);

   nouveau_ws_context_destroy(ctx);
   nouveau_ws_device_destroy(dev);
   return ret;
}