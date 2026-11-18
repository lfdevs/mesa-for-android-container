/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/libcl/libcl.h"

/* Triangle fans in the gpu_prerast chunk loop (v10+ CSF), next to the
 * struct panlib_chunk_draw of the draw, see fan_chunks.cl.
 *
 * Each chunk is an indexed fan of 32-bit indices in a per-draw buffer of
 * max_verts + 1 entries: the hub, then a window of API vertices. A window
 * that cuts a fan keeps its hub for the next window, which starts on the
 * window's last vertex, so every API triangle is drawn once with its own
 * vertices, winding and primitive ID.
 */
#define PANLIB_FAN_NO_HUB 0xffffffffu

struct panlib_fan_chunk {
   /* Chunk index buffer, max_verts + 1 uint32_t. */
   uint64_t indices;
   /* Position of the hub of the fan the last window cut, else
    * PANLIB_FAN_NO_HUB. Positions count from firstIndex (indexed) or 0. */
   uint32_t hub;
   /* The current chunk: index 0 is position c_hub, index j > 0 is
    * position c_first + j - 1, c_count indices. */
   uint32_t c_hub;
   uint32_t c_first;
   uint32_t c_count;
} PACKED;
