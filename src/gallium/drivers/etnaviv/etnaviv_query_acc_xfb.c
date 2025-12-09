/*
 * Copyright © 2025 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "hw/state_3d.xml.h"
#include "etnaviv_context.h"
#include "etnaviv_emit.h"
#include "etnaviv_query_acc.h"

static bool
xfb_supports(struct etna_context *ctx, unsigned query_type)
{
   if (!VIV_FEATURE(ctx->screen, ETNA_FEATURE_HWTFB))
      return false;

   switch (query_type) {
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      return true;
   default:
      return false;
   }
}

static struct etna_acc_query *
xfb_allocate(UNUSED struct etna_context *ctx, UNUSED unsigned query_type)
{
   return CALLOC_STRUCT(etna_acc_query);
}

static void
xfb_resume(struct etna_acc_query *aq, struct etna_context *ctx)
{
   struct etna_buffer_resource *rsc = etna_buffer_resource(aq->prsc);
   struct etna_reloc r = {
      .bo = rsc->bo,
      .flags = ETNA_RELOC_WRITE
   };

   etna_set_state_reloc(ctx->stream, VIVS_TFB_QUERY_BUFFER, &r);
   etna_set_state(ctx->stream, VIVS_TFB_QUERY_COMMAND, TFB_QUERY_COMMAND_ENABLE);
   resource_written(ctx, aq->prsc);
}

static void
xfb_suspend(struct etna_acc_query *aq, struct etna_context *ctx)
{
   etna_set_state(ctx->stream, VIVS_TFB_QUERY_COMMAND, TFB_QUERY_COMMAND_DISABLE);
   etna_set_state(ctx->stream, VIVS_TFB_FLUSH, 0x1);
   resource_written(ctx, aq->prsc);
}

static bool
xfb_result(UNUSED struct etna_acc_query *aq, void *buf,
           union pipe_query_result *result)
{
   /* The GPU stores the final results at offset 0 - no need
    * to do any manual accumulation.
    */
   result->u64 = *(uint64_t *)buf;

   return true;
}

const struct etna_acc_sample_provider xfb_provider = {
   .supports = xfb_supports,
   .allocate = xfb_allocate,
   .suspend = xfb_suspend,
   .resume = xfb_resume,
   .result = xfb_result,
};
