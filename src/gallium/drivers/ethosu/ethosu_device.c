/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "ethosu_device.h"
#include "ethosu_ml.h"

#include "drm-uapi/ethosu_accel.h"

#include <xf86drm.h>
#include "util/os_mman.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "util/u_transfer.h"

static const struct debug_named_value ethosu_debug_options[] = {
   {"dbg_msgs", ETHOSU_DBG_MSGS, "Print debug messages"},
   {"dump_bos", ETHOSU_DBG_DUMP_BOS, "Dump buffers for analysis"},
   {"zero_bos", ETHOSU_DBG_ZERO, "Zero buffers for debugging"},
   {"disable_nhcwb16", ETHOSU_DBG_DISABLE_NHCWB16, "Disable NHCWB16"},
   {"disable_sram", ETHOSU_DBG_DISABLE_SRAM, "Disable SRAM"},
   DEBUG_NAMED_VALUE_END};

DEBUG_GET_ONCE_FLAGS_OPTION(ethosu_debug, "ETHOSU_DEBUG", ethosu_debug_options, 0)
int ethosu_debug = 0;

static void
ethosu_destroy_screen(struct pipe_screen *pscreen)
{
   struct ethosu_screen *screen = ethosu_screen(pscreen);

   ralloc_free(screen);
}

static void
ethosu_destroy_context(struct pipe_context *pctx)
{
   struct ethosu_context *ctx = ethosu_context(pctx);

   ralloc_free(ctx);
}

static void *
ethosu_buffer_map(struct pipe_context *pctx,
                  struct pipe_resource *prsc, unsigned level,
                  unsigned usage, const struct pipe_box *box,
                  struct pipe_transfer **out_transfer)
{
   struct ethosu_screen *screen = ethosu_screen(pctx->screen);
   struct ethosu_resource *rsc = ethosu_resource(prsc);
   struct drm_ethosu_bo_wait bo_wait = {0};
   struct drm_ethosu_bo_mmap_offset bo_mmap_offset = {0};
   int ret;

   assert(level == 0);
   assert(prsc->target == PIPE_BUFFER);
   assert(box->y == 0);
   assert(box->z == 0);
   assert(box->height == 1);
   assert(box->depth == 1);

   struct pipe_transfer *transfer = rzalloc(NULL, struct pipe_transfer);
   transfer->level = level;
   transfer->usage = usage;
   transfer->box = *box;

   pipe_resource_reference(&transfer->resource, prsc);

   bo_wait.handle = rsc->handle;
   bo_wait.timeout_ns = INT64_MAX;

   ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_BO_WAIT, &bo_wait);
   if (ret == -1)
      goto free_transfer;

   bo_mmap_offset.handle = rsc->handle;
   ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_BO_MMAP_OFFSET, &bo_mmap_offset);
   if (ret == -1)
      goto free_transfer;

   uint8_t *map = os_mmap(NULL, prsc->width0, PROT_READ | PROT_WRITE, MAP_SHARED,
                          screen->fd, bo_mmap_offset.offset);
   assert(map != MAP_FAILED);
   if (map == MAP_FAILED)
      goto free_transfer;

   *out_transfer = transfer;

   return map + box->x;

free_transfer:
   pipe_resource_reference(&transfer->resource, NULL);
   ralloc_free(transfer);
   return NULL;
}

static void
ethosu_buffer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *transfer)
{
   pipe_resource_reference(&transfer->resource, NULL);
   ralloc_free(transfer);
}

static struct pipe_context *
ethosu_create_context(struct pipe_screen *screen,
                      void *priv, unsigned flags)
{
   struct ethosu_context *ctx = rzalloc(NULL, struct ethosu_context);
   struct pipe_context *pctx = &ctx->base;

   if (!ctx)
      return NULL;

   pctx->screen = screen;
   pctx->priv = priv;

   pctx->destroy = ethosu_destroy_context;

   pctx->buffer_map = ethosu_buffer_map;
   pctx->buffer_unmap = ethosu_buffer_unmap;
   pctx->resource_copy_region = util_resource_copy_region;
   pctx->buffer_subdata = u_default_buffer_subdata;
   pctx->clear_buffer = u_default_clear_buffer;

   pctx->ml_operation_supported = ethosu_ml_operation_supported;
   pctx->ml_subgraph_create = ethosu_ml_subgraph_create;
   pctx->ml_subgraph_invoke = ethosu_ml_subgraph_invoke;
   pctx->ml_subgraph_read_output = ethosu_ml_subgraph_read_outputs;
   pctx->ml_subgraph_destroy = ethosu_ml_subgraph_destroy;

   return pctx;
}

static struct pipe_resource *
ethosu_resource_create(struct pipe_screen *pscreen,
                       const struct pipe_resource *templat)
{
   struct ethosu_screen *screen = ethosu_screen(pscreen);
   struct drm_ethosu_bo_create arg = {0};
   struct ethosu_resource *rsc;
   int ret;

   assert(templat->target == PIPE_BUFFER);
   assert(templat->height0 == 1);
   assert(templat->depth0 == 1);
   assert(templat->array_size == 1);

   rsc = rzalloc(NULL, struct ethosu_resource);
   if (!rsc)
      return NULL;

   rsc->base = *templat;
   rsc->base.screen = pscreen;
   rsc->base.nr_samples = templat->nr_samples;
   pipe_reference_init(&rsc->base.reference, 1);

   rsc->bo_size = templat->width0;

   arg.size = templat->width0;

   ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_BO_CREATE, &arg);
   if (ret < 0)
      goto free_rsc;

   rsc->handle = arg.handle;

   return &rsc->base;

free_rsc:
   ralloc_free(rsc);
   return NULL;
}

static void
ethosu_resource_destroy(struct pipe_screen *pscreen,
                        struct pipe_resource *prsc)
{
   struct ethosu_resource *rsc = ethosu_resource(prsc);
   struct ethosu_screen *screen = ethosu_screen(pscreen);
   struct drm_gem_close arg = {0};
   int ret;

   arg.handle = rsc->handle;

   ret = drmIoctl(screen->fd, DRM_IOCTL_GEM_CLOSE, &arg);
   assert(ret >= 0);

   ralloc_free(rsc);
}

static int
ethosu_screen_get_fd(struct pipe_screen *pscreen)
{
   return ethosu_screen(pscreen)->fd;
}

static void
dev_query(struct ethosu_screen *screen)
{
   int ret;
   struct drm_ethosu_npu_info *info = &screen->info;
   struct drm_ethosu_dev_query dev_query = {
      .type = DRM_ETHOSU_DEV_QUERY_NPU_INFO,
      .size = sizeof(*info),
      .pointer = (uintptr_t)info,
   };

   ret = drmIoctl(screen->fd, DRM_IOCTL_ETHOSU_DEV_QUERY, &dev_query);
   assert(ret != -1);
}

struct pipe_screen *
ethosu_screen_create(int fd,
                     const struct pipe_screen_config *config,
                     struct renderonly *ro)
{
   struct ethosu_screen *ethosu_screen;
   struct pipe_screen *screen;

   ethosu_screen = rzalloc(NULL, struct ethosu_screen);
   if (!ethosu_screen)
      return NULL;

   screen = &ethosu_screen->pscreen;

   ethosu_debug = debug_get_option_ethosu_debug();

   ethosu_screen->fd = fd;
   dev_query(ethosu_screen);

   if (DBG_ENABLED(ETHOSU_DBG_DISABLE_SRAM))
      ethosu_screen->info.sram_size = 0;

   screen->get_screen_fd = ethosu_screen_get_fd;
   screen->destroy = ethosu_destroy_screen;
   screen->context_create = ethosu_create_context;
   screen->resource_create = ethosu_resource_create;
   screen->resource_destroy = ethosu_resource_destroy;

   return screen;
}