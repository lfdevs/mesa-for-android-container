/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <stdint.h>

#include "brw_reg.h"
#include "intel_mipmap_tree.h"

struct brw_context;
struct brw_wm_prog_key;

#ifdef __cplusplus
extern "C" {
#endif

void
brw_blorp_blit_miptrees(struct brw_context *brw,
                        struct intel_mipmap_tree *src_mt,
                        unsigned src_level, unsigned src_layer,
                        mesa_format src_format, int src_swizzle,
                        struct intel_mipmap_tree *dst_mt,
                        unsigned dst_level, unsigned dst_layer,
                        mesa_format dst_format,
                        float src_x0, float src_y0,
                        float src_x1, float src_y1,
                        float dst_x0, float dst_y0,
                        float dst_x1, float dst_y1,
                        GLenum filter, bool mirror_x, bool mirror_y,
                        bool decode_srgb, bool encode_srgb);

bool
brw_blorp_clear_color(struct brw_context *brw, struct gl_framebuffer *fb,
                      GLbitfield mask, bool partial_clear, bool encode_srgb);

void
brw_blorp_resolve_color(struct brw_context *brw,
                        struct intel_mipmap_tree *mt);

/**
 * Binding table indices used by BLORP.
 */
enum {
   BRW_BLORP_RENDERBUFFER_BINDING_TABLE_INDEX,
   BRW_BLORP_TEXTURE_BINDING_TABLE_INDEX,
   BRW_BLORP_NUM_BINDING_TABLE_ENTRIES
};

struct brw_blorp_surface_info
{
   struct intel_mipmap_tree *mt;

   /**
    * The miplevel to use.
    */
   uint32_t level;

   /**
    * The 2D layer within the miplevel. Combined, level and layer define the
    * 2D miptree slice to use.
    *
    * Note: if mt is a 2D multisample array texture on Gen7+ using
    * INTEL_MSAA_LAYOUT_UMS or INTEL_MSAA_LAYOUT_CMS, layer is the physical
    * layer holding sample 0.  So, for example, if mt->num_samples == 4, then
    * logical layer n corresponds to layer == 4*n.
    */
   uint32_t layer;

   /**
    * Width of the miplevel to be used.  For surfaces using
    * INTEL_MSAA_LAYOUT_IMS, this is measured in samples, not pixels.
    */
   uint32_t width;

   /**
    * Height of the miplevel to be used.  For surfaces using
    * INTEL_MSAA_LAYOUT_IMS, this is measured in samples, not pixels.
    */
   uint32_t height;

   /**
    * X offset within the surface to texture from (or render to).  For
    * surfaces using INTEL_MSAA_LAYOUT_IMS, this is measured in samples, not
    * pixels.
    */
   uint32_t x_offset;

   /**
    * Y offset within the surface to texture from (or render to).  For
    * surfaces using INTEL_MSAA_LAYOUT_IMS, this is measured in samples, not
    * pixels.
    */
   uint32_t y_offset;

   /* Setting this flag indicates that the buffer's contents are W-tiled
    * stencil data, but the surface state should be set up for Y tiled
    * MESA_FORMAT_R_UNORM8 data (this is necessary because surface states don't
    * support W tiling).
    *
    * Since W tiles are 64 pixels wide by 64 pixels high, whereas Y tiles of
    * MESA_FORMAT_R_UNORM8 data are 128 pixels wide by 32 pixels high, the width and
    * pitch stored in the surface state will be multiplied by 2, and the
    * height will be halved.  Also, since W and Y tiles store their data in a
    * different order, the width and height will be rounded up to a multiple
    * of the tile size, to ensure that the WM program can access the full
    * width and height of the buffer.
    */
   bool map_stencil_as_y_tiled;

   unsigned num_samples;

   /**
    * Indicates if we use the standard miptree layout (ALL_LOD_IN_EACH_SLICE),
    * or if we tightly pack array slices at each LOD (ALL_SLICES_AT_EACH_LOD).
    *
    * If ALL_SLICES_AT_EACH_LOD is set, then ARYSPC_LOD0 can be used. Ignored
    * prior to Gen7.
    */
   enum miptree_array_layout array_layout;

   /**
    * Format that should be used when setting up the surface state for this
    * surface.  Should correspond to one of the BRW_SURFACEFORMAT_* enums.
    */
   uint32_t brw_surfaceformat;

   /**
    * For MSAA surfaces, MSAA layout that should be used when setting up the
    * surface state for this surface.
    */
   enum intel_msaa_layout msaa_layout;

   /**
    * In order to support cases where RGBA format is backing client requested
    * RGB, one needs to have means to force alpha channel to one when user
    * requested RGB surface is used as blit source. This is possible by
    * setting source swizzle for the texture surface.
    */
   int swizzle;
};

void
brw_blorp_surface_info_init(struct brw_context *brw,
                            struct brw_blorp_surface_info *info,
                            struct intel_mipmap_tree *mt,
                            unsigned int level, unsigned int layer,
                            mesa_format format, bool is_render_target);

uint32_t
brw_blorp_compute_tile_offsets(const struct brw_blorp_surface_info *info,
                               uint32_t *tile_x, uint32_t *tile_y);



struct brw_blorp_coord_transform
{
   float multiplier;
   float offset;
};

struct brw_blorp_wm_push_constants
{
   uint32_t dst_x0;
   uint32_t dst_x1;
   uint32_t dst_y0;
   uint32_t dst_y1;
   /* Top right coordinates of the rectangular grid used for scaled blitting */
   float rect_grid_x1;
   float rect_grid_y1;
   struct brw_blorp_coord_transform x_transform;
   struct brw_blorp_coord_transform y_transform;

   /* Minimum layer setting works for all the textures types but texture_3d
    * for which the setting has no effect. Use the z-coordinate instead.
    */
   uint32_t src_z;

   /* Pad out to an integral number of registers */
   uint32_t pad[5];
};

#define BRW_BLORP_NUM_PUSH_CONSTANT_DWORDS \
   (sizeof(struct brw_blorp_wm_push_constants) / 4)

/* Every 32 bytes of push constant data constitutes one GEN register. */
static const unsigned int BRW_BLORP_NUM_PUSH_CONST_REGS =
   sizeof(struct brw_blorp_wm_push_constants) / 32;

struct brw_blorp_prog_data
{
   bool dispatch_8;
   bool dispatch_16;

   uint8_t first_curbe_grf_0;
   uint8_t first_curbe_grf_2;

   uint32_t ksp_offset_2;

   /**
    * True if the WM program should be run in MSDISPMODE_PERSAMPLE with more
    * than one sample per pixel.
    */
   bool persample_msaa_dispatch;

   /* The compiler will re-arrange push constants and store the upload order
    * here. Given an index 'i' in the final upload buffer, param[i] gives the
    * index in the uniform store. In other words, the value to be uploaded can
    * be found by brw_blorp_params::wm_push_consts[param[i]].
    */
   uint8_t nr_params;
   uint8_t param[BRW_BLORP_NUM_PUSH_CONSTANT_DWORDS];
};

struct brw_blorp_params
{
   uint32_t x0;
   uint32_t y0;
   uint32_t x1;
   uint32_t y1;
   struct brw_blorp_surface_info depth;
   uint32_t depth_format;
   struct brw_blorp_surface_info src;
   struct brw_blorp_surface_info dst;
   enum gen6_hiz_op hiz_op;
   union {
      unsigned fast_clear_op;
      unsigned resolve_type;
   };
   bool color_write_disable[4];
   struct brw_blorp_wm_push_constants wm_push_consts;
   unsigned num_varyings;
   unsigned num_draw_buffers;
   unsigned num_layers;
   uint32_t wm_prog_kernel;
   struct brw_blorp_prog_data *wm_prog_data;
};

void
brw_blorp_params_init(struct brw_blorp_params *params);

void
brw_blorp_exec(struct brw_context *brw, const struct brw_blorp_params *params);

void
gen6_blorp_hiz_exec(struct brw_context *brw, struct intel_mipmap_tree *mt,
                    unsigned level, unsigned layer, enum gen6_hiz_op op);

void
gen6_blorp_exec(struct brw_context *brw,
                const struct brw_blorp_params *params);

void
gen7_blorp_exec(struct brw_context *brw,
                const struct brw_blorp_params *params);

void
gen8_blorp_exec(struct brw_context *brw, const struct brw_blorp_params *params);

struct brw_blorp_blit_prog_key
{
   /* Number of samples per pixel that have been configured in the surface
    * state for texturing from.
    */
   unsigned tex_samples;

   /* MSAA layout that has been configured in the surface state for texturing
    * from.
    */
   enum intel_msaa_layout tex_layout;

   /* Actual number of samples per pixel in the source image. */
   unsigned src_samples;

   /* Actual MSAA layout used by the source image. */
   enum intel_msaa_layout src_layout;

   /* Number of samples per pixel that have been configured in the render
    * target.
    */
   unsigned rt_samples;

   /* MSAA layout that has been configured in the render target. */
   enum intel_msaa_layout rt_layout;

   /* Actual number of samples per pixel in the destination image. */
   unsigned dst_samples;

   /* Actual MSAA layout used by the destination image. */
   enum intel_msaa_layout dst_layout;

   /* Type of the data to be read from the texture (one of
    * BRW_REGISTER_TYPE_{UD,D,F}).
    */
   enum brw_reg_type texture_data_type;

   /* True if the source image is W tiled.  If true, the surface state for the
    * source image must be configured as Y tiled, and tex_samples must be 0.
    */
   bool src_tiled_w;

   /* True if the destination image is W tiled.  If true, the surface state
    * for the render target must be configured as Y tiled, and rt_samples must
    * be 0.
    */
   bool dst_tiled_w;

   /* True if all source samples should be blended together to produce each
    * destination pixel.  If true, src_tiled_w must be false, tex_samples must
    * equal src_samples, and tex_samples must be nonzero.
    */
   bool blend;

   /* True if the rectangle being sent through the rendering pipeline might be
    * larger than the destination rectangle, so the WM program should kill any
    * pixels that are outside the destination rectangle.
    */
   bool use_kill;

   /**
    * True if the WM program should be run in MSDISPMODE_PERSAMPLE with more
    * than one sample per pixel.
    */
   bool persample_msaa_dispatch;

   /* True for scaled blitting. */
   bool blit_scaled;

   /* Scale factors between the pixel grid and the grid of samples. We're
    * using grid of samples for bilinear filetring in multisample scaled blits.
    */
   float x_scale;
   float y_scale;

   /* True for blits with filter = GL_LINEAR. */
   bool bilinear_filter;
};

/**
 * \name BLORP internals
 * \{
 *
 * Used internally by gen6_blorp_exec() and gen7_blorp_exec().
 */

void brw_blorp_init_wm_prog_key(struct brw_wm_prog_key *wm_key);

const unsigned *
brw_blorp_compile_nir_shader(struct brw_context *brw, struct nir_shader *nir,
                             const struct brw_wm_prog_key *wm_key,
                             bool use_repclear,
                             struct brw_blorp_prog_data *prog_data,
                             unsigned *program_size);

void
gen6_blorp_init(struct brw_context *brw);

void
gen6_blorp_emit_vertices(struct brw_context *brw,
                         const struct brw_blorp_params *params);

uint32_t
gen6_blorp_emit_blend_state(struct brw_context *brw,
                            const struct brw_blorp_params *params);

uint32_t
gen6_blorp_emit_cc_state(struct brw_context *brw);

uint32_t
gen6_blorp_emit_wm_constants(struct brw_context *brw,
                             const struct brw_blorp_params *params);

void
gen6_blorp_emit_vs_disable(struct brw_context *brw,
                           const struct brw_blorp_params *params);

uint32_t
gen6_blorp_emit_binding_table(struct brw_context *brw,
                              uint32_t wm_surf_offset_renderbuffer,
                              uint32_t wm_surf_offset_texture);

uint32_t
gen6_blorp_emit_depth_stencil_state(struct brw_context *brw,
                                    const struct brw_blorp_params *params);

void
gen6_blorp_emit_gs_disable(struct brw_context *brw,
                           const struct brw_blorp_params *params);

void
gen6_blorp_emit_clip_disable(struct brw_context *brw);

void
gen6_blorp_emit_drawing_rectangle(struct brw_context *brw,
                                  const struct brw_blorp_params *params);

uint32_t
gen6_blorp_emit_sampler_state(struct brw_context *brw,
                              unsigned tex_filter, unsigned max_lod,
                              bool non_normalized_coords);
void
gen7_blorp_emit_urb_config(struct brw_context *brw);

void
gen7_blorp_emit_blend_state_pointer(struct brw_context *brw,
                                    uint32_t cc_blend_state_offset);

void
gen7_blorp_emit_cc_state_pointer(struct brw_context *brw,
                                 uint32_t cc_state_offset);

void
gen7_blorp_emit_cc_viewport(struct brw_context *brw);

void
gen7_blorp_emit_te_disable(struct brw_context *brw);

void
gen7_blorp_emit_binding_table_pointers_ps(struct brw_context *brw,
                                          uint32_t wm_bind_bo_offset);

void
gen7_blorp_emit_sampler_state_pointers_ps(struct brw_context *brw,
                                          uint32_t sampler_offset);

void
gen7_blorp_emit_clear_params(struct brw_context *brw,
                             const struct brw_blorp_params *params);

void
gen7_blorp_emit_constant_ps(struct brw_context *brw,
                            uint32_t wm_push_const_offset);

void
gen7_blorp_emit_constant_ps_disable(struct brw_context *brw);

void
gen7_blorp_emit_primitive(struct brw_context *brw,
                          const struct brw_blorp_params *params);

/** \} */

#ifdef __cplusplus
} /* end extern "C" */
#endif /* __cplusplus */
