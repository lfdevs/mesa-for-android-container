/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "renderonly/renderonly.h"
#include "util/log.h"

#ifndef RKT_SCREEN_H
#define RKT_SCREEN_H

enum rkt_dbg {
   ROCKET_DBG_MSGS = BITFIELD_BIT(0),
   ROCKET_DBG_DUMP_BOS = BITFIELD_BIT(1),
   ROCKET_DBG_ZERO = BITFIELD_BIT(2),
};

extern int rocket_debug;

#define DBG_ENABLED(flag) unlikely(rocket_debug &(flag))

#define DBG(fmt, ...)                                 \
   do {                                               \
      if (DBG_ENABLED(ROCKET_DBG_MSGS))               \
         mesa_logd("%s:%d: " fmt, __func__, __LINE__, \
                   ##__VA_ARGS__);                    \
   } while (0)

struct rkt_screen {
   struct pipe_screen pscreen;

   int fd;
   struct renderonly *ro;
};

static inline struct rkt_screen *
rkt_screen(struct pipe_screen *p)
{
   return (struct rkt_screen *)p;
}

struct rkt_context {
   struct pipe_context base;
};

static inline struct rkt_context *
rkt_context(struct pipe_context *pctx)
{
   return (struct rkt_context *)pctx;
}

struct rkt_resource {
   struct pipe_resource base;

   uint32_t handle;
   uint64_t phys_addr;
   uint64_t obj_addr;
   uint64_t fake_offset;
   uint64_t bo_size;
};

static inline struct rkt_resource *
rkt_resource(struct pipe_resource *p)
{
   return (struct rkt_resource *)p;
}

struct pipe_screen *rkt_screen_create(int fd,
                                      const struct pipe_screen_config *config,
                                      struct renderonly *ro);

#endif /* RKT_SCREEN_H */
