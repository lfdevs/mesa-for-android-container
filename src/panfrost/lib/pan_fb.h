/*
 * Copyright (C) 2026 Collabora, Ltd.
 * Copyright (C) 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_FB_H
#define __PAN_FB_H

#include "genxml/gen_macros.h"
#include "util/format/u_formats.h"
#include "compiler/shader_enums.h"

struct nir_shader;
struct nir_shader_compiler_options;
struct pan_fb_info;
struct pan_image_view;

#define PAN_MAX_RTS 8

/** Bounding box used by the framebuffer abstraction */
struct pan_fb_bbox {
   /** Minimum x/y value */
   uint16_t min_x, min_y;

   /** Maximum x/y value
    *
    * Like the hardware, the maximums here are inclusive.  A pixel is
    * in-bounds if min_x <= x <= max_x and min_y <= y <= max_y.
    */
   uint16_t max_x, max_y;
};

static inline bool
pan_fb_bbox_is_valid(struct pan_fb_bbox b)
{
   return b.min_x <= b.max_x && b.min_y <= b.max_y;
}

static inline bool
pan_fb_bbox_contains_bbox(struct pan_fb_bbox a, struct pan_fb_bbox b)
{
   return a.min_x <= b.min_x && b.max_x <= a.max_x &&
          a.min_y <= b.min_y && b.max_y <= a.max_y;
}

static inline bool
pan_fb_bbox_equal(struct pan_fb_bbox a, struct pan_fb_bbox b)
{
   return a.min_x == b.min_x && b.max_x == a.max_x &&
          a.min_y == b.min_y && b.max_y == a.max_y;
}

static inline struct pan_fb_bbox
pan_fb_bbox_clamp(struct pan_fb_bbox a, struct pan_fb_bbox b)
{
   assert(pan_fb_bbox_is_valid(a) && pan_fb_bbox_is_valid(a));
   return (struct pan_fb_bbox) {
      .min_x = CLAMP(a.min_x, b.min_x, b.max_x),
      .min_y = CLAMP(a.min_y, b.min_y, b.max_y),
      .max_x = CLAMP(a.max_x, b.min_x, b.max_x),
      .max_y = CLAMP(a.max_y, b.min_y, b.max_y),
   };
}

static inline struct pan_fb_bbox
pan_fb_bbox_align(struct pan_fb_bbox b, uint32_t align_x, uint32_t align_y)
{
   assert(util_is_power_of_two_nonzero(align_x));
   assert(util_is_power_of_two_nonzero(align_y));
   assert(align_x <= UINT16_MAX && align_y <= UINT16_MAX);

   b.min_x = ROUND_DOWN_TO(b.min_x, align_x);
   b.min_y = ROUND_DOWN_TO(b.min_y, align_y);
   b.max_x = align((uint32_t)b.max_x + 1, align_x) - 1;
   b.max_y = align((uint32_t)b.max_y + 1, align_y) - 1;

   return b;
}

static inline struct pan_fb_bbox
pan_fb_bbox_from_xywh(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   assert(x + w > x && x + w - 1 <= UINT16_MAX);
   assert(y + h > y && y + h - 1 <= UINT16_MAX);
   return (struct pan_fb_bbox) {
      .min_x = x,
      .min_y = y,
      .max_x = x + w - 1,
      .max_y = y + h - 1,
   };
}

/** Framebuffer layout
 *
 * This describes the layout of framebuffer in tile memory as well as various
 * properties that are properties of the framebuffer itself, nit any of the
 * image views attached to it.
 */
struct pan_fb_layout {
   /** Dimensions of the framebuffer itself, in pixels */
   uint32_t width_px, height_px;

   /** The render area
    *
    * This is the API render area (as defined by Vulkan) and may be smaller
    * than the actual bounding box of the framebuffer.  The in_bounds_load
    * and border_load members of pan_fb_load_target are relative to the
    * render area.
    */
   struct pan_fb_bbox render_area_px;

   /** The tiling area
    *
    * This is based on the render area but may be larger to accomodate tiling
    * restrictions.
    */
   struct pan_fb_bbox tiling_area_px;

   /** Sample count for this framebuffer */
   uint8_t sample_count;

   /** Color render target formats
    *
    * If a render target is unused, set it to PIPE_FORMAT_NONE.
    */
   uint8_t rt_count;
   enum pipe_format rt_formats[PAN_MAX_RTS];

   /** Depth format for this framebuffer, or PIPE_FORMAT_NONE */
   enum pipe_format z_format;

   /** Stencil format for this framebuffer, or PIPE_FORMAT_NONE */
   enum pipe_format s_format;

   /** Amount of space reserved for PLS, in bytes.
    *
    * This may overlap the color render targets in tile memory.
    */
   uint16_t pls_size_B;

   uint32_t tile_rt_budget_B;
   uint32_t tile_z_budget_B;

   /** Optimal tile buffer size, in pixels
    *
    * This must be a power of two.
    */
   unsigned tile_size_px;

   /** Amount of tile memory allocated for color render targets */
   unsigned tile_rt_alloc_B;
};

static inline bool
pan_fb_has_zs(const struct pan_fb_layout *fb)
{
   /* An intentional choice is made here to base has_zs on the formats in
    * the pan_fb_layout and not the stores being done in the end.  PanVK in
    * particular relies on the size of the final framebuffer descriptor being
    * the same for all the different variants with incremental rendering.
    */
   return fb->z_format != PIPE_FORMAT_NONE ||
          fb->s_format != PIPE_FORMAT_NONE;
}

static inline bool
pan_fb_is_fully_covered(const struct pan_fb_layout *fb)
{
   const struct pan_fb_bbox fb_area_px =
      pan_fb_bbox_from_xywh(0, 0, fb->width_px, fb->height_px);
   return pan_fb_bbox_equal(fb->render_area_px, fb_area_px);
}

static inline bool
pan_fb_has_partial_tiles(const struct pan_fb_layout *fb)
{
   assert(pan_fb_bbox_contains_bbox(fb->tiling_area_px, fb->render_area_px));
   return !pan_fb_bbox_equal(fb->tiling_area_px, fb->render_area_px);
}

#ifdef PAN_ARCH
void GENX(pan_select_fb_tile_size)(struct pan_fb_layout *fb);
#endif

enum ENUM_PACKED pan_fb_load_op {
   PAN_FB_LOAD_NONE = 0,
   PAN_FB_LOAD_CLEAR,
   PAN_FB_LOAD_IMAGE,
   PAN_FB_LOAD_OP_COUNT,
};

/** Describes how MSAA is handled when copying
 *
 * The source or destination may or may not be multisampled independently and
 * this controls how that is handled in the copy.
 */
enum ENUM_PACKED pan_fb_msaa_copy_op {
   /** Copies all samples of the source to the destination
    *
    * This requires that the source and destination sample counts be the same.
    * If the source and destination are both single-sampled, this is equivalent
    * as PAN_FB_MSAA_COPY_SINGLE or PAN_FB_MSAA_COPY_SAMPLE_0.
    */
   PAN_FB_MSAA_COPY_ALL = 0,

   /** Copies the single sample from the source to the destination
    *
    * This requires that the source be single-sampled.  Semantically, this is
    * the same as PAN_FB_MSAA_COPY_SAMPLE_0, but it's useful to make this
    * distinction in shader keys so single and multi-sampled sources can be
    * differentiated.
    */
   PAN_FB_MSAA_COPY_SINGLE,

   /** Copies assuming all source samples are identical
    *
    * The actual copy can use any resolve mode because we know a priori that
    * all samples are identical.  All resolve modes yield the same result.
    */
   PAN_FB_MSAA_COPY_IDENTICAL,

   /** Copies sample 0 from the source to all samples in the destination */
   PAN_FB_MSAA_COPY_SAMPLE_0,

   /** Copies the average of the samples in the source to the destination */
   PAN_FB_MSAA_COPY_AVERAGE,

   /** Copies the minimum of the samples in the source to the destination */
   PAN_FB_MSAA_COPY_MIN,

   /** Copies the maximum of the samples in the source to the destination */
   PAN_FB_MSAA_COPY_MAX,

   PAN_FB_MSAA_COPY_OP_COUNT,
};

struct pan_fb_load_target {
   /** Load op for pixels inside the bounding box */
   enum pan_fb_load_op in_bounds_load;

   /** Load op for border (out of bounds) pixels
    *
    * This should almost always be LOAD_OP_LOAD, unless you have a very good
    * reason to make a different choice.
    */
   enum pan_fb_load_op border_load;

   /** Always load, even if no primitives intersect the tile
    *
    * This should usually be set for clears.
    */
   bool always;

   /** MSAA mode used when load op is IMAGE */
   enum pan_fb_msaa_copy_op msaa;

   /* Image view to load from, or NULL if neither load op is IMAGE */
   const struct pan_image_view *iview;

   /** Clear value, if either load op is CLEAR */
   union {
      union pipe_color_union color;
      float depth;
      uint8_t stencil;
   } clear;
};

static inline struct pan_fb_load_target
pan_fb_load_iview(const struct pan_image_view *iview)
{
   return (struct pan_fb_load_target) {
      .in_bounds_load = PAN_FB_LOAD_IMAGE,
      .border_load = PAN_FB_LOAD_IMAGE,
      .msaa = PAN_FB_MSAA_COPY_ALL,
      .iview = iview,
   };
}

struct pan_fb_load {
   /** Color render target loads */
   struct pan_fb_load_target rts[PAN_MAX_RTS];

   /** Depth target load */
   struct pan_fb_load_target z;

   /** Stencil target load */
   struct pan_fb_load_target s;
};

static inline bool
pan_fb_has_image_load(const struct pan_fb_load *load, bool include_border)
{
   if (load->z.in_bounds_load == PAN_FB_LOAD_IMAGE ||
       load->s.in_bounds_load == PAN_FB_LOAD_IMAGE)
      return true;

   if (include_border &&
       (load->z.border_load == PAN_FB_LOAD_IMAGE ||
        load->s.border_load == PAN_FB_LOAD_IMAGE))
      return true;

   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
      if (load->rts[rt].in_bounds_load == PAN_FB_LOAD_IMAGE)
         return true;
      if (include_border && load->rts[rt].border_load == PAN_FB_LOAD_IMAGE)
         return true;
   }

   return false;
}

/** Describes a resolve operation
 *
 * This describes the source data for the resolve operation.  The way in which
 * MSAA is handled in the source data is defined by the pan_fb_msaa_copy_op.
 */
enum ENUM_PACKED pan_fb_resolve_op {
   /** Do nothing
    *
    * In this mode, the framebuffer is left unaffected by the resolve.  If
    * necessary (sometimes required for Z/S), the old value will be read and
    * written back to ensure the final value is the same.
    */
   PAN_FB_RESOLVE_NONE = 0,

   /** Load from the specified image view */
   PAN_FB_RESOLVE_IMAGE,

   /** Load from color render target 0 */
   PAN_FB_RESOLVE_RT_0,
   PAN_FB_RESOLVE_RT_1,
   PAN_FB_RESOLVE_RT_2,
   PAN_FB_RESOLVE_RT_3,
   PAN_FB_RESOLVE_RT_4,
   PAN_FB_RESOLVE_RT_5,
   PAN_FB_RESOLVE_RT_6,
   PAN_FB_RESOLVE_RT_7,

   /** Load from the depth target
    *
    * From the perspective of resolve ops, Z/S are always separate
    */
   PAN_FB_RESOLVE_Z,

   /** Load from the stencil target
    *
    * From the perspective of resolve ops, Z/S are always separate
    */
   PAN_FB_RESOLVE_S,
   PAN_FB_RESOLVE_OP_COUNT,
};

static_assert(PAN_FB_RESOLVE_Z == PAN_FB_RESOLVE_RT_0 + PAN_MAX_RTS,
              "There are PAN_MAX_RTS many RTs");

#define PAN_FB_RESOLVE_RT(rt) \
   (assert(0 <= (rt) && (rt) < PAN_MAX_RTS), (PAN_FB_RESOLVE_RT_0 + (rt)))

/** Describes a resovle operation on a given target
 *
 * For each side of the render area (in-bounds or border), this defines both
 * a source to copy from (the resolve op) and a MSAA copy op to apply as part
 * of the copy.  In the common case, a resolve op will read from itself but
 * that is not strictly a requirement.  Any resolve op can read from any
 * render target.
 */
struct pan_fb_resolve_target {
   struct pan_fb_resolve_op_msaa {
      enum pan_fb_resolve_op resolve;
      enum pan_fb_msaa_copy_op msaa;
   } in_bounds, border;

   /** For PAN_FB_RESOLVE_IMAGE the image view to load from */
   const struct pan_image_view *iview;
};

/** Describes a resolve operation
 *
 * A resolve operation is implemented as a post-frame shader (see
 * pan_fb_resolve_shader_key_fill) and does a copy from render targets to
 * render targets, applying MSAA copy ops along the way.
 */
struct pan_fb_resolve {
   struct pan_fb_resolve_target rts[PAN_MAX_RTS];
   struct pan_fb_resolve_target z, s;
};

struct pan_fb_store_target {
   /** Whether or not to do a store */
   bool store;

   /** Always store, even if no primitives intersect the tile
    *
    * clean_tile_write_enable will be set if either store.always is set or
    * load.always is set for the corresponding load[s].
    */
   bool always;

   /** MSAA mode to use when copying to the destination image
    *
    * This can only be set to AVERAGE for blendable color formats.
    */
   enum pan_fb_msaa_copy_op msaa;

   /** Image view to store to */
   const struct pan_image_view *iview;
};

static inline struct pan_fb_store_target
pan_fb_store_iview(const struct pan_image_view *iview)
{
   return (struct pan_fb_store_target) {
      .store = true,
      .msaa = PAN_FB_MSAA_COPY_ALL,
      .iview = iview,
   };
}

static inline struct pan_fb_store_target
pan_fb_always_store_iview_s0(const struct pan_image_view *iview)
{
   return (struct pan_fb_store_target) {
      .store = true,
      .always = true,
      .msaa = PAN_FB_MSAA_COPY_SAMPLE_0,
      .iview = iview,
   };
}

struct pan_fb_store {
   /* Color render target stores */
   struct pan_fb_store_target rts[PAN_MAX_RTS];

   /* Depth/stencil target store
    *
    * If the attached image view has a combined depth/stencil format, both
    * depth and stencil will be written.
    */
   struct pan_fb_store_target zs;

   /* Stencil target store */
   struct pan_fb_store_target s;
};

#ifdef PAN_ARCH
void GENX(pan_align_fb_tiling_area)(struct pan_fb_layout *fb,
                                    const struct pan_fb_store *store);

void GENX(pan_fb_fold_resolve_into_store)(const struct pan_fb_layout *fb,
                                          struct pan_fb_resolve *resolve,
                                          struct pan_fb_store *store);
#endif

struct pan_fb_frame_shaders {
   uint64_t dcd_pointer;
   uint8_t modes[3];
};

struct pan_fb_desc_info {
   const struct pan_fb_layout *fb;
   const struct pan_fb_load *load;
   const struct pan_fb_store *store;

   struct pan_fb_frame_shaders frame_shaders;

   uint64_t sample_pos_array_pointer;

   /* Only used on Valhal */
   bool sprite_coord_origin_max_y;
   bool provoking_vertex_first;
   bool allow_hsr_prepass;

   uint16_t layer;

   const struct pan_tls_info *tls;
   const struct pan_tiler_context *tiler_ctx;
};

#ifdef PAN_ARCH
void GENX(pan_fill_fb_info)(const struct pan_fb_desc_info *info,
                            struct pan_fb_info *fbinfo);

struct pan_fb_descs {
#if PAN_ARCH <= 13
   struct mali_framebuffer_packed *fbd;
#endif
   struct mali_zs_crc_extension_packed *zs_crc;
   struct mali_rgb_render_target_packed *rts;
};

uint32_t GENX(pan_emit_fb_desc)(const struct pan_fb_desc_info *info,
                                const struct pan_fb_descs *out);
#endif

enum ENUM_PACKED pan_fb_shader_op {
   /** Preserves the current fragment contents
    *
    * The MSAA mode is ignored and, if a load does need to occur to preserve
    * the current fragment contents, PAN_FB_MSAA_COPY_ALL is implied.
    *
    * This is only allowed in resolve shaders so this can always be replaced
    * with a discard.
    */
   PAN_FB_SHADER_PRESERVE = 0,

   /** Do whatever is the fastest (lowest bandwidth)
    *
    * This could be a CLEAR or PRESERVE or just write zeros.  It will never
    * write actual random data but the exact behavior may depend on other bits
    * in the shader key.
    */
   PAN_FB_SHADER_DONT_CARE,

   /** Loads the clear color
    *
    * This is only allowed in preload shaders and the clear will also be done
    * in hardware so it can always be replaced with a discard.
    */
   PAN_FB_SHADER_LOAD_CLEAR,

   /** Loads from the image bound at the corresponding gl_frag_result */
   PAN_FB_SHADER_LOAD_IMAGE,

   PAN_FB_SHADER_COPY_RT_0,
   PAN_FB_SHADER_COPY_RT_1,
   PAN_FB_SHADER_COPY_RT_2,
   PAN_FB_SHADER_COPY_RT_3,
   PAN_FB_SHADER_COPY_RT_4,
   PAN_FB_SHADER_COPY_RT_5,
   PAN_FB_SHADER_COPY_RT_6,
   PAN_FB_SHADER_COPY_RT_7,
   PAN_FB_SHADER_COPY_Z,
   PAN_FB_SHADER_COPY_S,

   PAN_FB_SHADER_OP_COUNT,
};

static_assert(PAN_FB_SHADER_COPY_Z == PAN_FB_SHADER_COPY_RT_0 + PAN_MAX_RTS,
              "We should have one PAN_FB_SHADER_COPY_RT_N per RT");

#define PAN_FB_SHADER_COPY_RT(rt) \
   (assert(0 <= (rt) && (rt) < PAN_MAX_RTS), (PAN_FB_SHADER_COPY_RT_0 + (rt)))

static inline bool
pan_fb_shader_op_can_discard(enum pan_fb_shader_op op)
{
   return op == PAN_FB_SHADER_PRESERVE ||
          op == PAN_FB_SHADER_DONT_CARE ||
          op == PAN_FB_SHADER_LOAD_CLEAR;
}

enum ENUM_PACKED pan_fb_shader_data_type {
   PAN_FB_SHADER_DATA_TYPE_F32 = 0,
   PAN_FB_SHADER_DATA_TYPE_I32,
   PAN_FB_SHADER_DATA_TYPE_U32,
   PAN_FB_SHADER_DATA_TYPE_COUNT,
};

static_assert(PAN_FB_SHADER_OP_COUNT <= (1 << 4),
              "pan_fb_shader_op fits in 4 bits");
static_assert(PAN_FB_MSAA_COPY_OP_COUNT <= (1 << 3),
              "pan_fb_msaa_copy_op fits in 3 bits");
static_assert(PAN_FB_SHADER_DATA_TYPE_COUNT <= (1 << 2),
              "pan_fb_shader_data_type fits in 2 bits");

/* Asserts for glsl_sampler_dim glsl_base_type have to be runtime because
 * there is no MAX value we can use.
 */

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct pan_fb_shader_key_target {
   uint32_t in_bounds_op : 4;
   uint32_t border_op : 4;
   uint32_t in_bounds_msaa : 3;
   uint32_t border_msaa : 3;
   uint32_t sample0_only : 1;
   uint32_t image_dim : 2;
   uint32_t image_is_array : 1;
   uint32_t image_samples_log2 : 3;
   uint32_t data_type : 2;
   uint32_t pad : 9;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct pan_fb_shader_key_target) == 4,
              "This struct has no holes");

/** Whether or not the given target is written by the FB shader
 *
 * GENX(pan_get_fb_shader)() guarantees that if both in_bounds_op and
 * border_op can be discarded, that it does nothing with that target.  This
 * way the full outputs_written can be known from just the shader key.
 */
static inline bool
pan_fb_shader_key_target_written(const struct pan_fb_shader_key_target *target)
{
   return !pan_fb_shader_op_can_discard(target->in_bounds_op) ||
          !pan_fb_shader_op_can_discard(target->border_op);
}

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct pan_fb_shader_key {
   struct pan_fb_shader_key_target rts[PAN_MAX_RTS];
   struct pan_fb_shader_key_target z, s;
   uint16_t z_format;
   uint8_t fb_sample_count;
   uint8_t _pad;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct pan_fb_shader_key) == 4 * (PAN_MAX_RTS + 3),
              "This struct has no holes");

#ifdef PAN_ARCH
bool GENX(pan_fb_load_shader_key_fill)(struct pan_fb_shader_key *key,
                                       const struct pan_fb_layout *fb,
                                       const struct pan_fb_load *load,
                                       bool zs_prepass);

#if PAN_ARCH >= 5
struct pan_fb_clean_tile {
   uint8_t rts;
   bool zs, s;
};

struct pan_fb_clean_tile
   GENX(pan_fb_get_clean_tile)(const struct pan_fb_desc_info *info);

static inline bool
pan_target_has_clear(const struct pan_fb_load_target *target)
{
   return target->in_bounds_load == PAN_FB_LOAD_CLEAR ||
          target->border_load == PAN_FB_LOAD_CLEAR;
}
#endif /* PAN_ARCH >= 5 */

#if PAN_ARCH >= 6
bool GENX(pan_fb_resolve_shader_key_fill)(struct pan_fb_shader_key *key,
                                          const struct pan_fb_layout *fb,
                                          const struct pan_fb_resolve *resolve);
#endif /* PAN_ARCH >= 6 */

struct nir_shader *
GENX(pan_get_fb_shader)(const struct pan_fb_shader_key *key,
                        const struct nir_shader_compiler_options *nir_options);

#if PAN_ARCH >= 13
/**
 * Returns true if there's enough space in the tile buffer for at least two
 * Z/S tiles.
 */
static inline bool
pan_fb_can_pipeline_zs(const struct pan_fb_layout *fb)
{
   const uint32_t z_B_per_px = sizeof(float) * fb->sample_count;
   const uint32_t z_B_per_tile = z_B_per_px * fb->tile_size_px;

   /* The budget is already half the available Z space */
   return z_B_per_tile < fb->tile_z_budget_B;
}
#endif
#endif /* PAN_ARCH */

#endif /* __PAN_FB_H */
