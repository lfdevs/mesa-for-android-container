/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * Copyright 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_CMDBUF_SDMA_H
#define AC_CMDBUF_SDMA_H

#include <stdbool.h>
#include "util/format/u_formats.h"

enum sdma_version;
struct radeon_info;
struct ac_cmdbuf;

#ifdef __cplusplus
extern "C" {
#endif

struct ac_sdma_surf {
   const struct radeon_surf *surf;
   uint64_t va;

   enum pipe_format img_format;
   enum pipe_format format;
   uint32_t bpp;

   struct {
      uint32_t x;
      uint32_t y;
      uint32_t z;
   } offset;

   bool is_compressed;
   bool is_secure;

   /* Linear */
   struct {
      uint32_t pitch;
      uint32_t slice_pitch;
   };

   /* Tiled */
   struct {
      bool is_stencil;

      struct {
         uint32_t width;
         uint32_t height;
         uint32_t depth;
      } extent;

      uint32_t first_level;
      uint32_t num_levels;

      uint64_t meta_va;
      uint32_t surf_type;
      bool htile_enabled;
   };
};

void ac_emit_sdma_nop(struct ac_cmdbuf *cs);

void ac_emit_sdma_write_timestamp(struct ac_cmdbuf *cs, uint64_t va);

void ac_emit_sdma_fence(struct ac_cmdbuf *cs, uint64_t va, uint32_t fence);

void ac_emit_sdma_wait_mem(struct ac_cmdbuf *cs, uint32_t op, uint64_t va, uint32_t ref, uint32_t mask);

void ac_emit_sdma_write_data_head(struct ac_cmdbuf *cs, uint64_t va, uint32_t count);

uint64_t
ac_emit_sdma_constant_fill(struct ac_cmdbuf *cs, enum sdma_version sdma_ip_version,
                           uint64_t va, uint64_t size, uint32_t value);

uint64_t
ac_emit_sdma_copy_linear(struct ac_cmdbuf *cs, enum sdma_version sdma_ip_version,
                         uint64_t src_va, uint64_t dst_va, uint64_t size,
                         bool tmz);

void
ac_emit_sdma_copy_linear_sub_window(struct ac_cmdbuf *cs, enum sdma_version sdma_ip_version,
                                    const struct ac_sdma_surf *src,
                                    const struct ac_sdma_surf *dst,
                                    uint32_t width, uint32_t height, uint32_t depth);

void
ac_emit_sdma_copy_tiled_sub_window(struct ac_cmdbuf *cs, const struct radeon_info *info,
                                   const struct ac_sdma_surf *linear,
                                   const struct ac_sdma_surf *tiled,
                                   bool detile, uint32_t width, uint32_t height,
                                   uint32_t depth, bool tmz);

void
ac_emit_sdma_copy_t2t_sub_window(struct ac_cmdbuf *cs, const struct radeon_info *info,
                                 const struct ac_sdma_surf *src,
                                 const struct ac_sdma_surf *dst,
                                 uint32_t width, uint32_t height, uint32_t depth);

#ifdef __cplusplus
}
#endif

#endif
