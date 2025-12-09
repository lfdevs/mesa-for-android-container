/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "rkt_device.h"
#include "rkt_ml.h"

#include "drm-uapi/rocket_accel.h"

#include <xf86drm.h>
#include "util/os_mman.h"
#include "util/ralloc.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "util/u_transfer.h"

static const struct debug_named_value rocket_debug_options[] = {
   {"dbg_msgs", ROCKET_DBG_MSGS, "Print debug messages"},
   {"dump_bos", ROCKET_DBG_DUMP_BOS, "Dump buffers for analysis"},
   {"zero_bos", ROCKET_DBG_ZERO, "Zero buffers for debugging"},
   DEBUG_NAMED_VALUE_END};

DEBUG_GET_ONCE_FLAGS_OPTION(rocket_debug, "ROCKET_DEBUG", rocket_debug_options, 0)
int rocket_debug = 0;

static void
rkt_destroy_screen(struct pipe_screen *pscreen)
{
   struct rkt_screen *screen = rkt_screen(pscreen);

   if (screen->ro)
      screen->ro->destroy(screen->ro);

   ralloc_free(screen);
}

static void
rkt_destroy_context(struct pipe_context *pctx)
{
   struct rkt_context *ctx = rkt_context(pctx);

   ralloc_free(ctx);
}

static void *
rkt_buffer_map(struct pipe_context *pctx,
               struct pipe_resource *prsc, unsigned level,
               unsigned usage, const struct pipe_box *box,
               struct pipe_transfer **out_transfer)
{
   struct rkt_screen *screen = rkt_screen(pctx->screen);
   struct rkt_resource *rsc = rkt_resource(prsc);
   struct drm_rocket_prep_bo arg = {0};
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

   arg.handle = rsc->handle;
   arg.timeout_ns = INT64_MAX;

   ret = drmIoctl(screen->fd, DRM_IOCTL_ROCKET_PREP_BO, &arg);
   assert(ret != -1);

   uint8_t *map = os_mmap(NULL, prsc->width0, PROT_READ | PROT_WRITE, MAP_SHARED,
                          screen->fd, rsc->fake_offset);
   assert(map != MAP_FAILED);

   *out_transfer = transfer;

   return map + box->x;
}

static void
rkt_buffer_unmap(struct pipe_context *pctx,
                 struct pipe_transfer *transfer)
{
   struct rkt_screen *screen = rkt_screen(pctx->screen);
   struct rkt_resource *rsrc = rkt_resource(transfer->resource);
   struct drm_rocket_fini_bo arg = {0};
   int ret;

   arg.handle = rsrc->handle;

   if (transfer->usage == PIPE_MAP_WRITE) {
      ret = drmIoctl(screen->fd, DRM_IOCTL_ROCKET_FINI_BO, &arg);
      assert(ret >= 0);
   }

   pipe_resource_reference(&transfer->resource, NULL);
   ralloc_free(transfer);
}

static struct pipe_context *
rkt_create_context(struct pipe_screen *screen,
                   void *priv, unsigned flags)
{
   struct rkt_context *ctx = rzalloc(NULL, struct rkt_context);
   struct pipe_context *pctx = &ctx->base;

   if (!ctx)
      return NULL;

   pctx->screen = screen;
   pctx->priv = priv;

   pctx->destroy = rkt_destroy_context;

   pctx->buffer_map = rkt_buffer_map;
   pctx->buffer_unmap = rkt_buffer_unmap;
   pctx->resource_copy_region = util_resource_copy_region;
   pctx->buffer_subdata = u_default_buffer_subdata;
   pctx->clear_buffer = u_default_clear_buffer;

   pctx->ml_operation_supported = rkt_ml_operation_supported;
   pctx->ml_subgraph_create = rkt_ml_subgraph_create;
   pctx->ml_subgraph_invoke = rkt_ml_subgraph_invoke;
   pctx->ml_subgraph_read_output = rkt_ml_subgraph_read_outputs;
   pctx->ml_subgraph_destroy = rkt_ml_subgraph_destroy;

   return pctx;
}

static struct pipe_resource *
rkt_resource_create(struct pipe_screen *pscreen,
                    const struct pipe_resource *templat)
{
   struct rkt_screen *screen = rkt_screen(pscreen);
   struct drm_rocket_create_bo arg = {0};
   struct rkt_resource *rsc;
   int ret;

   assert(templat->target == PIPE_BUFFER);
   assert(templat->height0 == 1);
   assert(templat->depth0 == 1);
   assert(templat->array_size == 1);

   rsc = rzalloc(NULL, struct rkt_resource);
   if (!rsc)
      return NULL;

   rsc->base = *templat;
   rsc->base.screen = pscreen;
   rsc->base.nr_samples = templat->nr_samples;
   pipe_reference_init(&rsc->base.reference, 1);

   rsc->bo_size = templat->width0;

   arg.size = templat->width0;

   ret = drmIoctl(screen->fd, DRM_IOCTL_ROCKET_CREATE_BO, &arg);
   if (ret < 0)
      goto free_rsc;

   rsc->handle = arg.handle;
   rsc->phys_addr = arg.dma_address;
   rsc->fake_offset = arg.offset;

   if (DBG_ENABLED(ROCKET_DBG_ZERO)) {
      void *map = os_mmap(NULL, arg.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          screen->fd, rsc->fake_offset);
      memset(map, 0, arg.size);
   }

   return &rsc->base;

free_rsc:
   ralloc_free(rsc);
   return NULL;
}

static void
rkt_resource_destroy(struct pipe_screen *pscreen,
                     struct pipe_resource *prsc)
{
   struct rkt_resource *rsc = rkt_resource(prsc);
   struct rkt_screen *screen = rkt_screen(pscreen);
   struct drm_gem_close arg = {0};
   int ret;

   arg.handle = rsc->handle;

   ret = drmIoctl(screen->fd, DRM_IOCTL_GEM_CLOSE, &arg);
   assert(ret >= 0);

   ralloc_free(rsc);
}

static int
rkt_screen_get_fd(struct pipe_screen *pscreen)
{
   return rkt_screen(pscreen)->fd;
}

struct pipe_screen *
rkt_screen_create(int fd,
                  const struct pipe_screen_config *config,
                  struct renderonly *ro)
{
   struct rkt_screen *rkt_screen;
   struct pipe_screen *screen;

   rkt_screen = rzalloc(NULL, struct rkt_screen);
   if (!rkt_screen)
      return NULL;

   screen = &rkt_screen->pscreen;

   rocket_debug = debug_get_option_rocket_debug();

   rkt_screen->fd = fd;

   screen->get_screen_fd = rkt_screen_get_fd;
   screen->destroy = rkt_destroy_screen;
   screen->context_create = rkt_create_context;
   screen->resource_create = rkt_resource_create;
   screen->resource_destroy = rkt_resource_destroy;

   return screen;
}