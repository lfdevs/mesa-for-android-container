/*
 * Copyright © 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/* jm-v7 091: the v6/v7 environment knobs that change a shader compile.
 * Each one is resolved once per process. panvk_hash_state() hashes the
 * resolved values, so a device A/B run with a knob toggled never gets a
 * binary compiled with the other setting from the disk cache.
 */

#ifndef PANVK_V7_KNOBS_H
#define PANVK_V7_KNOBS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* PANVK_TEX_WATERFALL: 0 = off, 1 = NonUniform divergent indices (default,
 * jm-v7 087), 2 = every divergent index ("all", jm-v7 085).
 */
static inline int
panvk_v7_knob_tex_waterfall(void)
{
   static int v = -1;
   if (v < 0) {
      const char *e = getenv("PANVK_TEX_WATERFALL");
      v = !e ? 1 : !strcmp(e, "0") ? 0 : !strcmp(e, "all") ? 2 : 1;
   }
   return v;
}

/* PANVK_V7_IDVS: -1 = default rules, 0 = never, 1 = also on quirked GPUs. */
static inline int
panvk_v7_knob_idvs(void)
{
   static int v = -2;
   if (v == -2) {
      const char *e = getenv("PANVK_V7_IDVS");
      v = e ? (e[0] == '1' ? 1 : 0) : -1;
   }
   return v;
}

/* PANVK_V7_DESC_REORDER: jm-v7 086, on unless "0". */
static inline bool
panvk_v7_knob_desc_reorder(void)
{
   static int v = -1;
   if (v < 0) {
      const char *e = getenv("PANVK_V7_DESC_REORDER");
      v = !(e && e[0] == '0');
   }
   return v;
}

/* Runtime-only knobs (no effect on a compile, so not hashed). */

/* Read "<name>=0" as off; default on. */
static inline bool
panvk_v7_knob_on(const char *name, int *cache)
{
   if (*cache < 0) {
      const char *e = getenv(name);
      *cache = !(e && e[0] == '0');
   }
   return *cache;
}

/* jm-v7 097: per-set descriptor dirty tracking, identical rebinds skipped. */
static inline bool
panvk_v7_knob_desc_dirty(void)
{
   static int v = -1;
   return panvk_v7_knob_on("PANVK_V7_DESC_DIRTY", &v);
}

/* jm-v7 097: push uniforms uploaded only when dirty. */
static inline bool
panvk_v7_knob_push_cache(void)
{
   static int v = -1;
   return panvk_v7_knob_on("PANVK_V7_PUSH_CACHE", &v);
}

/* jm-v7 097: direct-draw attribute tables reused while unchanged. */
static inline bool
panvk_v7_knob_attr_cache(void)
{
   static int v = -1;
   return panvk_v7_knob_on("PANVK_V7_ATTR_CACHE", &v);
}

/* jm-v7 099: one tiler heap BO per PANVK_OVERLAP half. Only with the
 * overlap on (PANVK_OVERLAP, default on), else there is one half.
 */
static inline bool
panvk_v7_split_heap(void)
{
   static int v = -1, ov = -1;
   return panvk_v7_knob_on("PANVK_V7_SPLIT_HEAP", &v) &&
          panvk_v7_knob_on("PANVK_OVERLAP", &ov);
}

/* jm-v7 105: the geometry emulation of a nested tess draw the CPU knows
 * is sized for its patches at the maximum tess level, not for
 * PANVK_TESS_GS_MAX_PRIMS.
 */
static inline bool
panvk_v7_tess_gs_tight(void)
{
   static int v = -1;
   return panvk_v7_knob_on("PANVK_V7_TESS_GS_TIGHT", &v);
}

/* jm-v7 107: batches of a command buffer share one TLS and one WLS buffer
 * per heap half. PANVK_V7_TLS_SHARE=0 = one per batch (old).
 */
static inline bool
panvk_v7_tls_share(void)
{
   static int v = -1;
   return panvk_v7_knob_on("PANVK_V7_TLS_SHARE", &v);
}

/* PANVK_OVERLAP (v67b, default on), as panvk_overlap_enabled(). */
static inline bool
panvk_v7_overlap_on(void)
{
   static int v = -1;
   return panvk_v7_knob_on("PANVK_OVERLAP", &v);
}

/* jm-v7 106: per-batch budget (MiB) of v6/v7 tessellation memory that is
 * reused by the next batch of the same heap half: worst-case tess eval
 * varyings and nested geometry emulation buffers. A tess draw that would
 * go past it starts a new batch. 0 = no budget (one batch, as before).
 */
static inline uint64_t
panvk_v7_tess_batch_budget(void)
{
   static int64_t v = -1;
   if (v < 0) {
      const char *e = getenv("PANVK_V7_TESS_BATCH_MB");
      v = e ? atoll(e) : 32;
      if (v < 0)
         v = 0;
   }
   return (uint64_t)v << 20;
}

#endif /* PANVK_V7_KNOBS_H */
