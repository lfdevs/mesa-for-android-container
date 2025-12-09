/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * Copyright 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_cmdbuf.h"
#include "ac_cmdbuf_sdma.h"
#include "ac_formats.h"
#include "ac_surface.h"
#include "sid.h"

#include "util/u_math.h"

void
ac_emit_sdma_nop(struct ac_cmdbuf *cs)
{
   /* SDMA NOP acts as a fence command and causes the SDMA engine to wait for pending copy operations. */
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
   ac_cmdbuf_end();
}

void
ac_emit_sdma_write_timestamp(struct ac_cmdbuf *cs, uint64_t va)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_TIMESTAMP, SDMA_TS_SUB_OPCODE_GET_GLOBAL_TIMESTAMP, 0));
   ac_cmdbuf_emit(va);
   ac_cmdbuf_emit(va >> 32);
   ac_cmdbuf_end();
}

void
ac_emit_sdma_fence(struct ac_cmdbuf *cs, uint64_t va, uint32_t fence)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_FENCE, 0, SDMA_FENCE_MTYPE_UC));
   ac_cmdbuf_emit(va);
   ac_cmdbuf_emit(va >> 32);
   ac_cmdbuf_emit(fence);
   ac_cmdbuf_end();
}

void
ac_emit_sdma_wait_mem(struct ac_cmdbuf *cs, uint32_t op, uint64_t va, uint32_t ref, uint32_t mask)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_POLL_REGMEM, 0, 0) | op << 28 | SDMA_POLL_MEM);
   ac_cmdbuf_emit(va);
   ac_cmdbuf_emit(va >> 32);
   ac_cmdbuf_emit(ref);
   ac_cmdbuf_emit(mask);
   ac_cmdbuf_emit(SDMA_POLL_INTERVAL_160_CLK | SDMA_POLL_RETRY_INDEFINITELY << 16);
   ac_cmdbuf_end();
}

void
ac_emit_sdma_write_data_head(struct ac_cmdbuf *cs, uint64_t va, uint32_t count)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
   ac_cmdbuf_emit(va);
   ac_cmdbuf_emit(va >> 32);
   ac_cmdbuf_emit(count - 1);
   ac_cmdbuf_end();
}

uint64_t
ac_emit_sdma_constant_fill(struct ac_cmdbuf *cs, enum sdma_version sdma_ip_version,
                           uint64_t va, uint64_t size, uint32_t value)
{
   const uint32_t fill_size = 2; /* This means that count is in DWORDS. */

   assert(sdma_ip_version >= SDMA_2_4);

   const uint64_t max_fill_size = BITFIELD64_MASK(sdma_ip_version >= SDMA_6_0 ? 30 : 22) & ~0x3;
   const uint64_t bytes_written = MIN2(size, max_fill_size);

   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_CONSTANT_FILL, 0, 0) | (fill_size << 30));
   ac_cmdbuf_emit(va);
   ac_cmdbuf_emit(va >> 32);
   ac_cmdbuf_emit(value);
   ac_cmdbuf_emit(bytes_written - 1); /* Must be programmed in bytes, even if the fill is done in dwords. */
   ac_cmdbuf_end();

   return bytes_written;
}

uint64_t
ac_emit_sdma_copy_linear(struct ac_cmdbuf *cs, enum sdma_version sdma_ip_version,
                         uint64_t src_va, uint64_t dst_va, uint64_t size,
                         bool tmz)
{
   const unsigned max_size_per_packet =
      sdma_ip_version >= SDMA_5_2 ? SDMA_V5_2_COPY_MAX_BYTES : SDMA_V2_0_COPY_MAX_BYTES;
   uint32_t align = ~0u;

   assert(sdma_ip_version >= SDMA_2_0);

   /* SDMA FW automatically enables a faster dword copy mode when
    * source, destination and size are all dword-aligned.
    *
    * When source and destination are dword-aligned, round down the size to
    * take advantage of faster dword copy, and copy the remaining few bytes
    * with the last copy packet.
    */
   if ((src_va & 0x3) == 0 && (dst_va & 0x3) == 0 && size > 4 && (size & 0x3) != 0) {
      align = ~0x3u;
   }

   const uint64_t bytes_written = size >= 4 ? MIN2(size & align, max_size_per_packet) : size;

   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_LINEAR, (tmz ? 4 : 0)));
   ac_cmdbuf_emit(sdma_ip_version >= SDMA_4_0 ? bytes_written - 1 : bytes_written);
   ac_cmdbuf_emit(0);
   ac_cmdbuf_emit(src_va);
   ac_cmdbuf_emit(src_va >> 32);
   ac_cmdbuf_emit(dst_va);
   ac_cmdbuf_emit(dst_va >> 32);
   ac_cmdbuf_end();

   return bytes_written;
}

static void
ac_sdma_check_pitches(uint32_t pitch, uint32_t slice_pitch, uint32_t bpp, bool uses_depth)
{
   ASSERTED const uint32_t pitch_alignment = MAX2(1, 4 / bpp);
   assert(pitch);
   assert(pitch <= (1 << 14));
   assert(util_is_aligned(pitch, pitch_alignment));

   if (uses_depth) {
      ASSERTED const uint32_t slice_pitch_alignment = 4;
      assert(slice_pitch);
      assert(slice_pitch <= (1 << 28));
      assert(util_is_aligned(slice_pitch, slice_pitch_alignment));
   }
}

void
ac_emit_sdma_copy_linear_sub_window(struct ac_cmdbuf *cs, enum sdma_version sdma_ip_version,
                                    const struct ac_sdma_surf_linear *src,
                                    const struct ac_sdma_surf_linear *dst,
                                    uint32_t width, uint32_t height, uint32_t depth)
{
   /* This packet is the same since SDMA v2.4, haven't bothered to check older versions.
    * The main difference is the bitfield sizes:
    *
    * v2.4 - src/dst_pitch: 14 bits, rect_z: 11 bits
    * v4.0 - src/dst_pitch: 19 bits, rect_z: 11 bits
    * v5.0 - src/dst_pitch: 19 bits, rect_z: 13 bits
    *
    * We currently use the smallest limits (from SDMA v2.4).
    */
   assert(src->bpp == dst->bpp);
   assert(util_is_power_of_two_nonzero(src->bpp));
   ac_sdma_check_pitches(src->pitch, src->slice_pitch, src->bpp, false);
   ac_sdma_check_pitches(dst->pitch, dst->slice_pitch, dst->bpp, false);

   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_LINEAR_SUB_WINDOW, 0) |
                  util_logbase2(src->bpp) << 29);
   ac_cmdbuf_emit(src->va);
   ac_cmdbuf_emit(src->va >> 32);
   ac_cmdbuf_emit(src->offset.x | src->offset.y << 16);
   ac_cmdbuf_emit(src->offset.z | (src->pitch - 1) << (sdma_ip_version >= SDMA_7_0 ? 16 : 13));
   ac_cmdbuf_emit(src->slice_pitch - 1);
   ac_cmdbuf_emit(dst->va);
   ac_cmdbuf_emit(dst->va >> 32);
   ac_cmdbuf_emit(dst->offset.x | dst->offset.y << 16);
   ac_cmdbuf_emit(dst->offset.z | (dst->pitch - 1) << (sdma_ip_version >= SDMA_7_0 ? 16 : 13));
   ac_cmdbuf_emit(dst->slice_pitch - 1);
   if (sdma_ip_version == SDMA_2_0) {
      ac_cmdbuf_emit(width | (height << 16));
      ac_cmdbuf_emit(depth);
   } else {
      ac_cmdbuf_emit((width - 1) | (height - 1) << 16);
      ac_cmdbuf_emit((depth - 1));
   }
   ac_cmdbuf_end();
}

static uint32_t
ac_sdma_get_tiled_header_dword(enum sdma_version sdma_ip_version,
                               const struct ac_sdma_surf_tiled *tiled)
{
   if (sdma_ip_version >= SDMA_5_0) {
      return 0;
   } else if (sdma_ip_version >= SDMA_4_0) {
      const uint32_t mip_max = MAX2(tiled->num_levels, 1);
      const uint32_t mip_id = tiled->first_level;

      return (mip_max - 1) << 20 | mip_id << 24;
   } else {
      UNREACHABLE("unsupported SDMA version");
   }
}

static enum gfx9_resource_type
ac_sdma_get_tiled_resource_dim(enum sdma_version sdma_ip_version,
                               const struct ac_sdma_surf_tiled *tiled)
{
   if (sdma_ip_version >= SDMA_5_0) {
      /* Use the 2D resource type for rotated or Z swizzles. */
      if ((tiled->surf->u.gfx9.resource_type == RADEON_RESOURCE_1D ||
           tiled->surf->u.gfx9.resource_type == RADEON_RESOURCE_3D) &&
          (tiled->surf->micro_tile_mode == RADEON_MICRO_MODE_RENDER ||
           tiled->surf->micro_tile_mode == RADEON_MICRO_MODE_DEPTH))
         return RADEON_RESOURCE_2D;
   }

   return tiled->surf->u.gfx9.resource_type;
}

static uint32_t
ac_sdma_get_tiled_info_dword(const struct radeon_info *info,
                             const struct ac_sdma_surf_tiled *tiled)
{
   const uint32_t swizzle_mode = tiled->surf->has_stencil ? tiled->surf->u.gfx9.zs.stencil_swizzle_mode
                                                          : tiled->surf->u.gfx9.swizzle_mode;
   const enum gfx9_resource_type dimension =
      ac_sdma_get_tiled_resource_dim(info->sdma_ip_version, tiled);
   const uint32_t mip_max = MAX2(tiled->num_levels, 1);
   const uint32_t mip_id = tiled->first_level;
   const uint32_t element_size = util_logbase2(tiled->bpp);
   uint32_t info_dword = 0;

   if (info->sdma_ip_version >= SDMA_4_0) {
      info_dword |= element_size;
      info_dword |= swizzle_mode << 3;

      if (info->sdma_ip_version >= SDMA_7_0) {
         return info_dword | (mip_max - 1) << 16 | mip_id << 24;
      } else if (info->sdma_ip_version >= SDMA_5_0) {
         return info_dword | dimension << 9 | (mip_max - 1) << 16 | mip_id << 20;
      } else {
         return info_dword | dimension << 9 | tiled->surf->u.gfx9.epitch << 16;
      }
   } else {
      const uint32_t tile_index = tiled->surf->u.legacy.tiling_index[0];
      const uint32_t macro_tile_index = tiled->surf->u.legacy.macro_tile_index;
      const uint32_t tile_mode = info->si_tile_mode_array[tile_index];
      const uint32_t macro_tile_mode = info->cik_macrotile_mode_array[macro_tile_index];

      return element_size |
             (G_009910_ARRAY_MODE(tile_mode) << 3) |
             (G_009910_MICRO_TILE_MODE_NEW(tile_mode) << 8) |
             /* Non-depth modes don't have TILE_SPLIT set. */
             ((util_logbase2(tiled->surf->u.legacy.tile_split >> 6)) << 11) |
             (G_009990_BANK_WIDTH(macro_tile_mode) << 15) |
             (G_009990_BANK_HEIGHT(macro_tile_mode) << 18) |
             (G_009990_NUM_BANKS(macro_tile_mode) << 21) |
             (G_009990_MACRO_TILE_ASPECT(macro_tile_mode) << 24) |
             (G_009910_PIPE_CONFIG(tile_mode) << 26);
   }
}

static uint32_t
ac_sdma_get_tiled_metadata_config(const struct radeon_info *info,
                                  const struct ac_sdma_surf_tiled *tiled,
                                  bool detile, bool tmz)
{
   const uint32_t data_format = ac_get_cb_format(info->gfx_level, tiled->format);
   const uint32_t number_type = ac_get_cb_number_type(tiled->format);
   const bool alpha_is_on_msb = ac_alpha_is_on_msb(info, tiled->format);
   const uint32_t dcc_max_compressed_block_size =
      tiled->surf->u.gfx9.color.dcc.max_compressed_block_size;

   if (info->sdma_ip_version >= SDMA_7_0) {
      return SDMA7_DCC_DATA_FORMAT(data_format) |
             SDMA7_DCC_NUM_TYPE(number_type) |
             SDMA7_DCC_MAX_COM(dcc_max_compressed_block_size) |
             SDMA7_DCC_READ_CM(2) |
             SDMA7_DCC_MAX_UCOM(1) |
             SDMA7_DCC_WRITE_CM(!detile);
   } else {
      const bool dcc_pipe_aligned = tiled->htile_enabled ||
                                    tiled->surf->u.gfx9.color.dcc.pipe_aligned;

      return SDMA5_DCC_DATA_FORMAT(data_format) |
             SDMA5_DCC_ALPHA_IS_ON_MSB(alpha_is_on_msb) |
             SDMA5_DCC_NUM_TYPE(number_type) |
             SDMA5_DCC_SURF_TYPE(tiled->surf_type) |
             SDMA5_DCC_MAX_COM(dcc_max_compressed_block_size) |
             SDMA5_DCC_PIPE_ALIGNED(dcc_pipe_aligned) |
             SDMA5_DCC_MAX_UCOM(V_028C78_MAX_BLOCK_SIZE_256B) |
             SDMA5_DCC_WRITE_COMPRESS(!detile) |
             SDMA5_DCC_TMZ(tmz);
   }
}

void
ac_emit_sdma_copy_tiled_sub_window(struct ac_cmdbuf *cs, const struct radeon_info *info,
                                   const struct ac_sdma_surf_linear *linear,
                                   const struct ac_sdma_surf_tiled *tiled,
                                   bool detile, uint32_t width, uint32_t height,
                                   uint32_t depth, bool tmz)
{
   const uint32_t header_dword =
      ac_sdma_get_tiled_header_dword(info->sdma_ip_version, tiled);
   const uint32_t info_dword =
      ac_sdma_get_tiled_info_dword(info, tiled);
   const bool dcc = tiled->is_compressed;

   /* Sanity checks. */
   const bool uses_depth = linear->offset.z != 0 || tiled->offset.z != 0 || depth != 1;
   assert(util_is_power_of_two_nonzero(tiled->bpp));
   ac_sdma_check_pitches(linear->pitch, linear->slice_pitch, tiled->bpp, uses_depth);
   if (!info->sdma_supports_compression)
      assert(!tiled->is_compressed);

   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_TILED_SUB_WINDOW, (tmz ? 4 : 0)) |
                  dcc << 19 | detile << 31 | header_dword);
   ac_cmdbuf_emit(tiled->va);
   ac_cmdbuf_emit(tiled->va >> 32);
   ac_cmdbuf_emit(tiled->offset.x | tiled->offset.y << 16);
   ac_cmdbuf_emit(tiled->offset.z | (tiled->extent.width - 1) << 16);
   ac_cmdbuf_emit((tiled->extent.height - 1) | (tiled->extent.depth - 1) << 16);
   ac_cmdbuf_emit(info_dword);
   ac_cmdbuf_emit(linear->va);
   ac_cmdbuf_emit(linear->va >> 32);
   ac_cmdbuf_emit(linear->offset.x | linear->offset.y << 16);
   ac_cmdbuf_emit(linear->offset.z | (linear->pitch - 1) << 16);
   ac_cmdbuf_emit(linear->slice_pitch - 1);
   if (info->sdma_ip_version == SDMA_2_0) {
      ac_cmdbuf_emit(width | (height << 16));
      ac_cmdbuf_emit(depth);
   } else {
      ac_cmdbuf_emit((width - 1) | (height - 1) << 16);
      ac_cmdbuf_emit((depth - 1));
   }

   if (tiled->is_compressed) {
      const uint32_t meta_config =
         ac_sdma_get_tiled_metadata_config(info, tiled, detile, tmz);

      if (info->sdma_ip_version >= SDMA_7_0) {
         ac_cmdbuf_emit(meta_config);
      } else {
         ac_cmdbuf_emit(tiled->meta_va);
         ac_cmdbuf_emit(tiled->meta_va >> 32);
         ac_cmdbuf_emit(meta_config);
      }
   }

   ac_cmdbuf_end();
}

void
ac_emit_sdma_copy_t2t_sub_window(struct ac_cmdbuf *cs, const struct radeon_info *info,
                                 const struct ac_sdma_surf_tiled *src,
                                 const struct ac_sdma_surf_tiled *dst,
                                 uint32_t width, uint32_t height, uint32_t depth)
{
   const uint32_t src_header_dword =
      ac_sdma_get_tiled_header_dword(info->sdma_ip_version, src);
   const uint32_t src_info_dword = ac_sdma_get_tiled_info_dword(info, src);
   const uint32_t dst_info_dword = ac_sdma_get_tiled_info_dword(info, dst);

   /* Sanity checks. */
   assert(info->sdma_ip_version >= SDMA_4_0);

   /* On GFX10+ this supports DCC, but cannot copy a compressed surface to another compressed surface. */
   assert(!src->is_compressed || !dst->is_compressed);

   if (info->sdma_ip_version >= SDMA_4_0 && info->sdma_ip_version < SDMA_5_0) {
      /* SDMA v4 doesn't support mip_id selection in the T2T copy packet. */
      assert(src_header_dword >> 24 == 0);
      /* SDMA v4 doesn't support any image metadata. */
      assert(!src->is_compressed);
      assert(!dst->is_compressed);
   }
   assert(util_is_power_of_two_nonzero(src->bpp));
   assert(util_is_power_of_two_nonzero(dst->bpp));

   /* Despite the name, this can indicate DCC or HTILE metadata. */
   const uint32_t dcc = src->is_compressed || dst->is_compressed;
   /* 0 = compress (src is uncompressed), 1 = decompress (src is compressed). */
   const uint32_t dcc_dir = src->is_compressed && !dst->is_compressed;

   ac_cmdbuf_begin(cs);
   ac_cmdbuf_emit(SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_T2T_SUB_WINDOW, 0) |
                  dcc << 19 | dcc_dir << 31 | src_header_dword);
   ac_cmdbuf_emit(src->va);
   ac_cmdbuf_emit(src->va >> 32);
   ac_cmdbuf_emit(src->offset.x | src->offset.y << 16);
   ac_cmdbuf_emit(src->offset.z | (src->extent.width - 1) << 16);
   ac_cmdbuf_emit((src->extent.height - 1) | (src->extent.depth - 1) << 16);
   ac_cmdbuf_emit(src_info_dword);
   ac_cmdbuf_emit(dst->va);
   ac_cmdbuf_emit(dst->va >> 32);
   ac_cmdbuf_emit(dst->offset.x | dst->offset.y << 16);
   ac_cmdbuf_emit(dst->offset.z | (dst->extent.width - 1) << 16);
   ac_cmdbuf_emit((dst->extent.height - 1) | (dst->extent.depth - 1) << 16);
   ac_cmdbuf_emit(dst_info_dword);
   ac_cmdbuf_emit((width - 1) | (height - 1) << 16);
   ac_cmdbuf_emit((depth - 1));

   if (info->sdma_ip_version >= SDMA_7_0) {
      /* Compress only when dst has DCC. If src has DCC, it automatically
       * decompresses according to PTE.D (page table bit) even if we don't
       * enable DCC in the packet.
       */
      if (dst->is_compressed) {
         const uint32_t dst_meta_config =
            ac_sdma_get_tiled_metadata_config(info, dst, false, false);

         ac_cmdbuf_emit(dst_meta_config);
      }
   } else {
      if (dst->is_compressed) {
         const uint32_t dst_meta_config =
            ac_sdma_get_tiled_metadata_config(info, dst, false, false);

         ac_cmdbuf_emit(dst->meta_va);
         ac_cmdbuf_emit(dst->meta_va >> 32);
         ac_cmdbuf_emit(dst_meta_config);
      } else if (src->is_compressed) {
         const uint32_t src_meta_config =
            ac_sdma_get_tiled_metadata_config(info, src, true, false);

         ac_cmdbuf_emit(src->meta_va);
         ac_cmdbuf_emit(src->meta_va >> 32);
         ac_cmdbuf_emit(src_meta_config);
      }
   }

   ac_cmdbuf_end();
}
