/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_DESCRIPTORS_H
#define AC_DESCRIPTORS_H

#include "ac_gpu_info.h"
#include "ac_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DUPL_16BITS_IN_DWORD(x) (((x) << 16) | (x))
#define DUPL_8BITS_IN_DWORD(x) (((x) << 24) | ((x) << 16) | ((x) << 8) | (x))
#define DUPL_4BITS_IN_DWORD(x) DUPL_8BITS_IN_DWORD((x) | ((x) << 4))

#define DCC_CODE                       DUPL_8BITS_IN_DWORD
#define CMASK_NOAA_CODE                DUPL_4BITS_IN_DWORD
#define CMASK_MSAA_CODE(fmask, color)  DUPL_4BITS_IN_DWORD((fmask) | ((color) << 2))

enum
{
   /* DCC clear codes for all generations. */
   DCC_CLEAR_0000    = DCC_CODE(0x00), /* all bits are 0 */
   DCC_UNCOMPRESSED  = DCC_CODE(0xFF),

   /* DCC clear codes for GFX8-10. */
   GFX8_DCC_CLEAR_0000     = DCC_CLEAR_0000,
   GFX8_DCC_CLEAR_0001     = DCC_CODE(0x40),
   GFX8_DCC_CLEAR_1110     = DCC_CODE(0x80),
   GFX8_DCC_CLEAR_1111     = DCC_CODE(0xC0),
   GFX8_DCC_CLEAR_REG      = DCC_CODE(0x20),
   GFX9_DCC_CLEAR_SINGLE   = DCC_CODE(0x10),

   /* DCC clear codes for GFX11. */
   GFX11_DCC_CLEAR_SINGLE     = DCC_CODE(0x01),
   GFX11_DCC_CLEAR_0000       = DCC_CLEAR_0000, /* all bits are 0 */
   GFX11_DCC_CLEAR_1111_UNORM = DCC_CODE(0x02), /* all bits are 1 */
   GFX11_DCC_CLEAR_1111_FP16  = DCC_CODE(0x04), /* all 16-bit words are 0x3c00, max 64bpp */
   GFX11_DCC_CLEAR_1111_FP32  = DCC_CODE(0x06), /* all 32-bit words are 0x3f800000 */
   /* Color bits are 0, alpha bits are 1; only 88, 8888, 16161616 */
   GFX11_DCC_CLEAR_0001_UNORM = DCC_CODE(0x08),
   /* Color bits are 1, alpha bits are 0, only 88, 8888, 16161616 */
   GFX11_DCC_CLEAR_1110_UNORM = DCC_CODE(0x0A),
};

enum {
   /* Legacy color clear and FMASK clear/compression.
    * CMASK determines whether color is cleared to a clear color in a register.
    * CMASK with MSAA also handles FMASK clears and compression.
    */
   CMASK_NOAA_COLOR_CLEAR_REG = CMASK_NOAA_CODE(0x0), /* only valid on gfx6-9, illegal with DCC */
   CMASK_NOAA_COLOR_EXPANDED  = CMASK_NOAA_CODE(0xF), /* only valid on gfx6-9 */

   /* The first value is FMASK compression code, the second value is fast clear. */
   CMASK_MSAA_FMASK_CLEAR_0_COLOR_CLEAR_REG        = CMASK_MSAA_CODE(0, 0), /* illegal with DCC */
   CMASK_MSAA_FMASK_CLEAR_0_COLOR_EXPANDED         = CMASK_MSAA_CODE(0, 3),
   /* Different MSAA modes require different CMASK codes for "FMASK uncompressed". */
   CMASK_2xMSAA_FMASK_UNCOMPRESSED_COLOR_EXPANDED  = CMASK_MSAA_CODE(1, 3),
   CMASK_4xMSAA_FMASK_UNCOMPRESSED_COLOR_EXPANDED  = CMASK_MSAA_CODE(2, 3),
   CMASK_8xMSAA_FMASK_UNCOMPRESSED_COLOR_EXPANDED  = CMASK_MSAA_CODE(3, 3),
};

enum {
   /* Don't ever use this. Clear CMASK instead. */
   FMASK_CLEAR_0 = 0,

   /* These can be used only if FMASK is uncompressed in CMASK.
    *
    * Uncompressed doesn't mean expanded.
    * - FMASK compression only affects bandwidth, not stored values. The compression is done by CMASK.
    * - Expanded FMASK means that specific values are stored in it such that FMASK has no effect.
    *   FMASK expansion is a layout transition only required before MSAA image stores.
    * - CB_FMASK_DECOMPRESS is a layout transition required before any shader access, and does:
    *     1. FMASK decompression: Eliminating CMASK compression.
    *     2. CMASK fast color clear elimination: Writing the clear value in CB_COLORi_CLEAR_WORDj
    *        registers to cleared areas of the color image.
    *     ! It doesn't do FMASK expansion, which must be done by a compute shader.
    * - GFX8-10.3: To avoid CB_FMASK_DECOMPRESS before shader access (except MSAA image stores):
    *    - Use FMASK with TC-compatible CMASK. (enabled by FMASK_COMPRESS_1FRAG_ONLY)
    *    - Use DCC for fast MSAA color clears instead of CMASK.
    */
   FMASK_2xMSAA_EXPANDED = DUPL_8BITS_IN_DWORD(0x02),
   FMASK_4xMSAA_EXPANDED = DUPL_8BITS_IN_DWORD(0xE4),
   FMASK_8xMSAA_EXPANDED = 0x76543210,

   FMASK_EQAA_2S_1F_EXPANDED = FMASK_2xMSAA_EXPANDED,
   FMASK_EQAA_4S_1F_EXPANDED = DUPL_8BITS_IN_DWORD(0x0E),
   FMASK_EQAA_8S_1F_EXPANDED = DUPL_8BITS_IN_DWORD(0xFE),
   FMASK_EQAA_16S_1F_EXPANDED = DUPL_16BITS_IN_DWORD(0xFFFE),

   FMASK_EQAA_4S_2F_EXPANDED = DUPL_8BITS_IN_DWORD(0xA4),
   FMASK_EQAA_8S_2F_EXPANDED = DUPL_16BITS_IN_DWORD(0xAAA4),
   FMASK_EQAA_16S_2F_EXPANDED = 0xAAAAAAA4,

   FMASK_EQAA_8S_4F_EXPANDED = 0x44443210,
   FMASK_EQAA_16S_4F_EXPANDED = 0x4444444444443210ull, /* 8-byte clear value */

   /* Enums don't allow such large numbers. */
   #define FMASK_EQAA_16S_8F_EXPANDED 0x8888888876543210ull /* 8-byte clear value */
};

typedef union {
   /* Z only */
   struct {
      unsigned zmask : 4;
      unsigned minz : 14;
      unsigned maxz : 14;
   } z;

   struct {
      unsigned zmask : 4;
      /* SR0/SR1 contain stencil pretest results. */
      unsigned sr0 : 2;
      unsigned sr1 : 2;
      unsigned smem : 2;
      unsigned unused : 2;
      /* The Z Range consists of a 6-bit delta and 14-bit base.
       * ZRANGE_PRECISION determines whether zbase means minZ or maxZ.
       */
      unsigned zdelta : 6;
      unsigned zbase : 14;
   } zs;

   /* Z + VRS. VRS fields are 0-based: (0, 0) means VRS 1x1. */
   struct {
      unsigned zmask : 4;
      unsigned sr0 : 2;
      unsigned vrs_x : 2;
      unsigned smem : 2;
      unsigned vrs_y : 2;
      unsigned zdelta : 6;
      unsigned zbase : 14;
   } zs_vrs;

   uint32_t dword;
} ac_htile_dword;

#define HTILE_Z_CODE(...)        ((ac_htile_dword){.z = {__VA_ARGS__}}).dword
#define HTILE_ZS_CODE(...)       ((ac_htile_dword){.zs = {__VA_ARGS__}}).dword
#define HTILE_ZS_VRS_CODE(...)   ((ac_htile_dword){.zs_vrs = {__VA_ARGS__}}).dword

/* depth must be in [0, 1]. This only clears HiZ and sets the Z/S state to "cleared".
 * The DB register contain the full clear values.
 */
#define HTILE_Z_CLEAR_REG(depth)  HTILE_Z_CODE( \
   .zmask = 0, \
   .minz = lroundf((depth) * 0x3FFF), \
   .maxz = lroundf((depth) * 0x3FFF))

#define HTILE_ZS_CLEAR_REG(depth)  HTILE_ZS_CODE( \
   .zmask = 0, \
   .sr0 = 0x3, \
   .sr1 = 0x3, \
   .smem = 0, \
   .zdelta = 0, \
   .zbase = lroundf((depth) * 0x3FFF))

#define HTILE_ZS_VRS_CLEAR_REG(depth)  HTILE_ZS_VRS_CODE( \
   .zmask = 0, \
   .sr0 = 0x3, \
   .smem = 0, \
   .zdelta = 0, \
   .zbase = lroundf((depth) * 0x3FFF), \
   .vrs_x = 0, /* VRS = 1x1 (0-based) */ \
   .vrs_y = 0)

/* Zmask = Z uncompressed, minZ = 0, maxZ = 1. */
#define HTILE_Z_UNCOMPRESSED  HTILE_Z_CODE( \
   .zmask = 0xF, \
   .minz = 0, \
   .maxz = 0x3FFF)

/* Zmask = Z uncompressed, SR0/SR1 = Stencil pretest is unknown, Smem = Stencil uncompressed,
 * ZRange = [0, 1].
 */
#define HTILE_ZS_UNCOMPRESSED  HTILE_ZS_CODE( \
   .zmask = 0xF, \
   .sr0 = 0x3, \
   .sr1 = 0x3, \
   .smem = 0x3, \
   .zdelta = 0x3F, \
   .zbase = 0x3FFF)

/* Zmask = Z uncompressed, SR0 = Stencil pretest is unknown, Smem = Stencil uncompressed,
 * ZRange = [0, 1], VRS = 1x1 (0-based).
 */
#define HTILE_ZS_VRS_UNCOMPRESSED  HTILE_ZS_VRS_CODE( \
   .zmask = 0xF, \
   .sr0 = 0x3, \
   .smem = 0x3, \
   .zdelta = 0x3F, \
   .zbase = 0x3FFF, \
   .vrs_x = 0, /* VRS = 1x1 (0-based) */ \
   .vrs_y = 0)

unsigned
ac_map_swizzle(unsigned swizzle);

struct ac_sampler_state {
   unsigned address_mode_u : 3;
   unsigned address_mode_v : 3;
   unsigned address_mode_w : 3;
   unsigned max_aniso_ratio : 3;
   unsigned depth_compare_func : 3;
   unsigned unnormalized_coords : 1;
   unsigned cube_wrap : 1;
   unsigned trunc_coord : 1;
   unsigned filter_mode : 2;
   unsigned mag_filter : 2;
   unsigned min_filter : 2;
   unsigned mip_filter : 2;
   unsigned aniso_single_level : 1;
   unsigned border_color_type : 2;
   unsigned border_color_ptr : 12;
   float min_lod;
   float max_lod;
   float lod_bias;
};

void
ac_build_sampler_descriptor(const enum amd_gfx_level gfx_level,
                            const struct ac_sampler_state *state,
                            uint32_t desc[4]);

struct ac_fmask_state {
   const struct radeon_surf *surf;
   uint64_t va;
   uint32_t width : 16;
   uint32_t height : 16;
   uint32_t depth : 14;
   uint32_t type : 4;
   uint32_t first_layer : 14;
   uint32_t last_layer : 13;

   uint32_t num_samples : 5;
   uint32_t num_storage_samples : 4;
   uint32_t tc_compat_cmask : 1;
};

void
ac_build_fmask_descriptor(const enum amd_gfx_level gfx_level,
                          const struct ac_fmask_state *state,
                          uint32_t desc[8]);

struct ac_texture_state {
   const struct radeon_surf *surf;
   enum pipe_format format;
   enum pipe_format img_format;
   uint32_t width : 17;
   uint32_t height : 17;
   uint32_t depth : 15;
   uint32_t type : 4;
   enum pipe_swizzle swizzle[4];
   uint32_t num_samples : 5;
   uint32_t num_storage_samples : 5;
   uint32_t first_level : 4;
   uint32_t last_level : 5;
   uint32_t num_levels : 6;
   uint32_t first_layer : 14;
   uint32_t last_layer : 13;
   float min_lod;

   struct {
      const struct ac_surf_nbc_view *nbc_view;
      uint32_t uav3d : 1;
      uint32_t upgraded_depth : 1;
   } gfx10;

   uint32_t dcc_enabled : 1;
   uint32_t tc_compat_htile_enabled : 1;
   uint32_t aniso_single_level : 1;
};

void
ac_build_texture_descriptor(const struct radeon_info *info,
                            const struct ac_texture_state *state,
                            uint32_t desc[8]);

uint32_t
ac_tile_mode_index(const struct radeon_surf *surf,
                   unsigned level,
                   bool stencil);

struct ac_mutable_tex_state {
   const struct radeon_surf *surf;
   uint64_t va;

   struct {
      const struct ac_surf_nbc_view *nbc_view;
      uint32_t write_compress_enable : 1;
      uint32_t iterate_256 : 1;
   } gfx10;

   struct {
      const struct legacy_surf_level *base_level_info;
      uint32_t base_level;
      uint32_t block_width;
   } gfx6;

   uint32_t is_stencil : 1;
   uint32_t dcc_enabled : 1;
   uint32_t tc_compat_htile_enabled : 1;
};

void
ac_set_mutable_tex_desc_fields(const struct radeon_info *info,
                               const struct ac_mutable_tex_state *state,
                               uint32_t desc[8]);

struct ac_buffer_state {
   uint64_t va;
   uint32_t size;
   enum pipe_format format;
   enum pipe_swizzle swizzle[4];
   uint32_t stride;
   uint32_t swizzle_enable : 2;
   uint32_t element_size : 2;
   uint32_t index_stride : 2;
   uint32_t add_tid : 1;
   uint32_t gfx10_oob_select : 2;

   struct {
      uint32_t compression_en : 1;
      uint32_t write_compress_enable : 1;
   } gfx12;
};

void
ac_set_buf_desc_word3(const enum amd_gfx_level gfx_level,
                      const struct ac_buffer_state *state,
                      uint32_t *rsrc_word3);

void
ac_build_buffer_descriptor(const enum amd_gfx_level gfx_level,
                           const struct ac_buffer_state *state,
                           uint32_t desc[4]);

void
ac_build_raw_buffer_descriptor(const enum amd_gfx_level gfx_level,
                               uint64_t va,
                               uint32_t size,
                               uint32_t desc[4]);

void
ac_build_attr_ring_descriptor(const enum amd_gfx_level gfx_level,
                              uint64_t va,
                              uint32_t size,
                              uint32_t stride,
                              uint32_t desc[4]);

struct ac_ds_state {
   const struct radeon_surf *surf;
   uint64_t va;
   enum pipe_format format;
   uint32_t width : 17;
   uint32_t height : 17;
   uint32_t level : 5;
   uint32_t num_levels : 6;
   uint32_t num_samples : 5;
   uint32_t first_layer : 14;
   uint32_t last_layer : 14;

   uint32_t allow_expclear : 1;
   uint32_t stencil_only : 1;
   uint32_t z_read_only : 1;
   uint32_t stencil_read_only : 1;

   uint32_t htile_enabled : 1;
   uint32_t htile_stencil_disabled : 1;
   uint32_t vrs_enabled : 1;
};

struct ac_ds_surface {
   uint64_t db_depth_base;
   uint64_t db_stencil_base;
   uint32_t db_depth_view;
   uint32_t db_depth_size;
   uint32_t db_z_info;
   uint32_t db_stencil_info;

   union {
      struct {
         uint64_t hiz_base;
         uint32_t hiz_info;
         uint32_t hiz_size_xy;
         uint32_t db_depth_view1;
      } gfx12;

      struct {
         uint64_t db_htile_data_base;
         uint32_t db_depth_info;
         uint32_t db_depth_slice;
         uint32_t db_htile_surface;
         uint32_t db_z_info2;
         uint32_t db_stencil_info2;
      } gfx6;
   } u;
};

void
ac_init_ds_surface(const struct radeon_info *info, const struct ac_ds_state *state, struct ac_ds_surface *ds);

struct ac_mutable_ds_state {
   const struct ac_ds_surface *ds; /* original DS surface */
   enum pipe_format format;
   uint32_t tc_compat_htile_enabled : 1;
   uint32_t zrange_precision : 1;
   uint32_t no_d16_compression : 1;
};

void
ac_set_mutable_ds_surface_fields(const struct radeon_info *info, const struct ac_mutable_ds_state *state,
                                 struct ac_ds_surface *ds);

struct ac_cb_state {
   const struct radeon_surf *surf;
   enum pipe_format format;
   uint32_t width : 17;
   uint32_t height : 17;
   uint32_t first_layer : 14;
   uint32_t last_layer : 14;
   uint32_t num_layers : 14;
   uint32_t num_samples : 5;
   uint32_t num_storage_samples : 5;
   uint32_t base_level : 5;
   uint32_t num_levels : 6;

   struct {
      struct ac_surf_nbc_view *nbc_view;
   } gfx10;
};

struct ac_cb_surface {
   uint32_t cb_color_info;
   uint32_t cb_color_view;
   uint32_t cb_color_view2;
   uint32_t cb_color_attrib;
   uint32_t cb_color_attrib2; /* GFX9+ */
   uint32_t cb_color_attrib3; /* GFX10+ */
   uint32_t cb_dcc_control;
   uint64_t cb_color_base;
   uint64_t cb_color_cmask;
   uint64_t cb_color_fmask;
   uint64_t cb_dcc_base;
   uint32_t cb_color_slice;
   uint32_t cb_color_cmask_slice;
   uint32_t cb_color_fmask_slice;
   union {
      uint32_t cb_color_pitch; /* GFX6-GFX8 */
      uint32_t cb_mrt_epitch;  /* GFX9+ */
   };
};

void
ac_init_cb_surface(const struct radeon_info *info, const struct ac_cb_state *state, struct ac_cb_surface *cb);

struct ac_mutable_cb_state {
   const struct radeon_surf *surf;
   const struct ac_cb_surface *cb; /* original CB surface */
   uint64_t va;

   uint32_t base_level : 5;
   uint32_t num_samples : 5;

   uint32_t fmask_enabled : 1;
   uint32_t cmask_enabled : 1;
   uint32_t fast_clear_enabled : 1;
   uint32_t tc_compat_cmask_enabled : 1;
   uint32_t dcc_enabled : 1;

   struct {
      struct ac_surf_nbc_view *nbc_view;
   } gfx10;
};

void
ac_set_mutable_cb_surface_fields(const struct radeon_info *info, const struct ac_mutable_cb_state *state,
                                 struct ac_cb_surface *cb);

struct ac_gfx12_hiz_state {
   const struct radeon_surf *surf;
   uint64_t va;
   uint32_t type : 4;
   uint32_t num_samples : 5;
   uint32_t first_level : 4;
   uint32_t last_level : 5;
   uint32_t num_levels : 6;
   uint32_t first_layer : 13;
   uint32_t last_layer : 13;
};

void
ac_build_gfx12_hiz_descriptor(const struct ac_gfx12_hiz_state *state, uint32_t desc[8]);

#ifdef __cplusplus
}
#endif

#endif
