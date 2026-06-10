/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "renderonly/renderonly.h"
#include "util/log.h"

#include "drm-uapi/ethosu_accel.h"

#ifndef ETHOSU_SCREEN_H
#define ETHOSU_SCREEN_H

enum ethosu_dbg {
   ETHOSU_DBG_MSGS = BITFIELD_BIT(0),
   ETHOSU_DBG_DUMP_BOS = BITFIELD_BIT(1),
   ETHOSU_DBG_ZERO = BITFIELD_BIT(2),
   ETHOSU_DBG_DISABLE_NHCWB16 = BITFIELD_BIT(3),
   ETHOSU_DBG_DISABLE_SRAM = BITFIELD_BIT(4),
   ETHOSU_DBG_FORCE_U85 = BITFIELD_BIT(5),
   ETHOSU_DBG_DUMP_PERF = BITFIELD_BIT(6),
};

extern int ethosu_debug;

#define DBG_ENABLED(flag) unlikely(ethosu_debug &(flag))

#define DBG(fmt, ...)                                 \
   do {                                               \
      if (DBG_ENABLED(ETHOSU_DBG_MSGS))               \
         mesa_logd("%s:%d: " fmt, __func__, __LINE__, \
                   ##__VA_ARGS__);                    \
   } while (0)

struct ethosu_block {
   unsigned width;
   unsigned height;
   unsigned depth;
};

struct ethosu_ml_device {
   struct pipe_ml_device base;

   /* Target hardware description — set from DRM query or from spec string */
   bool is_u65;
   struct ethosu_block ifm_ublock;
   struct ethosu_block ofm_ublock;
   unsigned max_concurrent_blocks;
   uint32_t sram_size;
};

struct ethosu_screen {
   struct pipe_screen pscreen;
   struct ethosu_ml_device ml_device;

   int fd;
   struct drm_ethosu_npu_info info;
};

static inline struct ethosu_screen *
ethosu_screen(struct pipe_screen *p)
{
   return (struct ethosu_screen *)p;
}

static inline struct ethosu_ml_device *
ethosu_ml_device(struct pipe_ml_device *p)
{
   return (struct ethosu_ml_device *)p;
}

struct ethosu_context {
   struct pipe_context base;
};

static inline struct ethosu_context *
ethosu_context(struct pipe_context *pctx)
{
   return (struct ethosu_context *)pctx;
}

struct ethosu_resource {
   struct pipe_resource base;

   uint32_t handle;
   uint64_t bo_size;
   void *map;
};

static inline struct ethosu_resource *
ethosu_resource(struct pipe_resource *p)
{
   return (struct ethosu_resource *)p;
}

struct pipe_screen *ethosu_screen_create(int fd,
                                         const struct pipe_screen_config *config,
                                         struct renderonly *ro);

#endif /* ETHOSU_SCREEN_H */
