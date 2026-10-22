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

#endif /* PANVK_V7_KNOBS_H */
