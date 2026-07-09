/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANFROST_PERFETTO_H_
#define PANFROST_PERFETTO_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_PERFETTO

/**
 * Render-stage id's
 */
enum panfrost_stage_id {
   PANFROST_STAGE_VERTEX_TILER, /* Vertex/tiler (draw) work */
   PANFROST_STAGE_FRAGMENT,     /* Fragment render pass with RT info */
   PANFROST_STAGE_COMPUTE,      /* Compute dispatch */
   PANFROST_STAGE_BATCH,        /* CSF-only: entire batch from first job to cache flush */
   PANFROST_STAGE_BARRIER,      /* CSF-only: GPU overhead waiting for all slots to drain */
   PANFROST_STAGE_CACHE_FLUSH,  /* CSF-only: GPU overhead flushing/invalidating caches */

   PANFROST_NUM_STAGES
};

/**
 * Hardware queue id's.
 *
 * Although Mali hardware exposes separate vertex/tiler, fragment and compute
 * queues, and that's is reflected on panvk, the panfrost Gallium driver
 * submits all work through a single queue, so we report a single default
 * queue here to match what the driver actually does.
 */
enum {
   PANFROST_DEFAULT_HW_QUEUE_ID,
};

/**
 * Per-context Perfetto state: only start timestamps. All payload data is
 * carried directly in the end-tracepoint struct and emitted via the
 * trace_payload_as_extra_end_[tp] generated helpers.
 */
struct panfrost_perfetto_state {
   uint64_t start_ts[PANFROST_NUM_STAGES];
   /* CPU timestamp after which the next GPU clock sync should be emitted.
    * Zero forces an immediate sync (on session start or reset).
    */
   uint64_t next_clock_sync_ns;
};

struct panfrost_device;
void panfrost_perfetto_init(struct panfrost_device *dev);

struct panfrost_context;
void panfrost_perfetto_submit(struct panfrost_context *ctx);

#endif /* HAVE_PERFETTO */

#ifdef __cplusplus
}
#endif

#endif /* PANFROST_PERFETTO_H_ */
