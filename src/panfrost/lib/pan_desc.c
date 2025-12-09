/*
 * Copyright (C) 2021 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *   Boris Brezillon <boris.brezillon@collabora.com>
 */

#include "util/macros.h"

#include "genxml/gen_macros.h"

#include "pan_afbc.h"
#include "pan_afrc.h"
#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_props.h"
#include "pan_texture.h"
#include "pan_util.h"

#define PAN_BIN_LEVEL_COUNT 12

static enum mali_msaa
mali_sampling_mode(const struct pan_image_view *view)
{
   unsigned nr_samples = pan_image_view_get_nr_samples(view);

   if (nr_samples > 1) {
      ASSERTED const struct pan_image_plane_ref pref =
         pan_image_view_get_first_plane(view);

      assert(view->nr_samples == pref.image->props.nr_samples);
      return MALI_MSAA_LAYERED;
   }

   if (view->nr_samples > nr_samples) {
      assert(nr_samples == 1);
      return MALI_MSAA_AVERAGE;
   }

   assert(view->nr_samples == nr_samples);
   assert(view->nr_samples == 1);

   return MALI_MSAA_SINGLE;
}

static bool
renderblock_fits_in_single_pass(const struct pan_image_view *view,
                                unsigned tile_size)
{
   const struct pan_image_plane_ref pref = pan_image_view_get_first_plane(view);
   uint64_t mod = pref.image->props.modifier;

   if (!drm_is_afbc(mod))
      return tile_size >= 16 * 16;

   struct pan_image_block_size renderblk_sz = pan_afbc_renderblock_size(mod);
   return tile_size >= renderblk_sz.width * renderblk_sz.height;
}

int
GENX(pan_select_crc_rt)(const struct pan_fb_info *fb, unsigned tile_size)
{
   /* Disable CRC when the tile size is smaller than 16x16. In the hardware,
    * CRC tiles are the same size as the tiles of the framebuffer. However,
    * our code only handles 16x16 tiles. Therefore under the current
    * implementation, we must disable CRC when 16x16 tiles are not used.
    *
    * This may hurt performance. However, smaller tile sizes are rare, and
    * CRCs are more expensive at smaller tile sizes, reducing the benefit.
    * Restricting CRC to 16x16 should work in practice.
    */
   if (tile_size < 16 * 16)
      return -1;

#if PAN_ARCH <= 6
   if (fb->rt_count == 1 && fb->rts[0].view && !fb->rts[0].discard &&
       pan_image_view_has_crc(fb->rts[0].view))
      return 0;

   return -1;
#else
   bool best_rt_valid = false;
   int best_rt = -1;

   for (unsigned i = 0; i < fb->rt_count; i++) {
      if (!fb->rts[i].view || fb->rts[i].discard ||
          !pan_image_view_has_crc(fb->rts[i].view))
         continue;

      if (!renderblock_fits_in_single_pass(fb->rts[i].view, tile_size))
         continue;

      bool valid = *(fb->rts[i].crc_valid);
      bool full = !fb->draw_extent.minx && !fb->draw_extent.miny &&
                  fb->draw_extent.maxx == (fb->width - 1) &&
                  fb->draw_extent.maxy == (fb->height - 1);
      if (!full && !valid)
         continue;

      if (best_rt < 0 || (valid && !best_rt_valid)) {
         best_rt = i;
         best_rt_valid = valid;
      }

      if (valid)
         break;
   }

   return best_rt;
#endif
}

static enum mali_zs_format
translate_zs_format(enum pipe_format in)
{
   switch (in) {
   case PIPE_FORMAT_Z16_UNORM:
      return MALI_ZS_FORMAT_D16;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return MALI_ZS_FORMAT_D24S8;
   case PIPE_FORMAT_Z24X8_UNORM:
      return MALI_ZS_FORMAT_D24X8;
   case PIPE_FORMAT_Z24_UNORM_PACKED:
      return MALI_ZS_FORMAT_D24;
   case PIPE_FORMAT_Z32_FLOAT:
      return MALI_ZS_FORMAT_D32;
#if PAN_ARCH < 9
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return MALI_ZS_FORMAT_D32_S8X24;
#endif
   default:
      UNREACHABLE("Unsupported depth/stencil format.");
   }
}

#if PAN_ARCH >= 5
static enum mali_s_format
translate_s_format(enum pipe_format in)
{
   switch (in) {
   case PIPE_FORMAT_S8_UINT:
      return MALI_S_FORMAT_S8;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_X24S8_UINT:
      return MALI_S_FORMAT_X24S8;

#if PAN_ARCH < 9
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
   case PIPE_FORMAT_S8X24_UINT:
      return MALI_S_FORMAT_S8X24;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
   case PIPE_FORMAT_X32_S8X24_UINT:
      return MALI_S_FORMAT_X32_S8X24;
#endif

   default:
      UNREACHABLE("Unsupported stencil format.");
   }
}

static void
get_tiled_or_linear_att_mem_props(struct pan_image_plane_ref pref,
                                  unsigned mip_level, unsigned layer_or_z_slice,
                                  uint64_t *base, uint64_t *row_stride,
                                  uint64_t *surf_stride)
{
   const struct pan_image *image = pref.image;
   const struct pan_image_plane *plane = image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];
   const unsigned array_idx =
      image->props.dim == MALI_TEXTURE_DIMENSION_3D ? 0 : layer_or_z_slice;
   const unsigned surf_idx =
      image->props.dim == MALI_TEXTURE_DIMENSION_3D ? layer_or_z_slice : 0;

   *base = plane->base + slayout->offset_B +
           (array_idx * plane->layout.array_stride_B) +
           (surf_idx * slayout->tiled_or_linear.surface_stride_B);
   *row_stride = slayout->tiled_or_linear.row_stride_B;
   *surf_stride = slayout->tiled_or_linear.surface_stride_B;
}

static enum mali_block_format
get_afbc_block_format(uint64_t mod)
{
#if PAN_ARCH >= 7
   if (mod & AFBC_FORMAT_MOD_TILED)
      return MALI_BLOCK_FORMAT_AFBC_TILED;
#endif

   assert(!(mod & AFBC_FORMAT_MOD_TILED));
   return MALI_BLOCK_FORMAT_AFBC;
}

static void
get_afbc_att_mem_props(struct pan_image_plane_ref pref, unsigned mip_level,
                       unsigned layer_or_z_slice, uint64_t *header,
                       uint64_t *body_offset, uint64_t *row_stride)
{
   const struct pan_image *image = pref.image;
   const struct pan_image_plane *plane = image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];
   const uint64_t stride_B = image->props.dim == MALI_TEXTURE_DIMENSION_3D
                                ? slayout->afbc.surface_stride_B
                                : plane->layout.array_stride_B;

   *row_stride = slayout->afbc.header.row_stride_B;
   *body_offset = pan_afbc_body_offset(PAN_ARCH, image->props.modifier,
                                       slayout->afbc.header.surface_size_B);
   *header = plane->base + slayout->offset_B + (stride_B * layer_or_z_slice);
}

#if PAN_ARCH <= 10
#define SET_SURFACE_STRIDE(cfg__, val__) (cfg__).surface_stride = val__
#else
#define SET_SURFACE_STRIDE(cfg__, val__)                                       \
   do {                                                                        \
      (cfg__).surface_stride = val__ & BITFIELD_MASK(32);                      \
      (cfg__).surface_stride_hi = val__ >> 32;                                 \
   } while (0)
#endif

void
GENX(pan_emit_linear_s_attachment)(const struct pan_fb_info *fb,
                                   unsigned layer_or_z_slice, void *payload)
{
   const struct pan_image_view *s = fb->zs.view.s;
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_s_plane(s),
                                     s->first_level, layer_or_z_slice, &base,
                                     &row_stride, &surf_stride);
   pan_cast_and_pack(payload, S_TARGET, cfg) {
      cfg.msaa = mali_sampling_mode(s);
      cfg.write_format = translate_s_format(s->format);
      cfg.block_format = MALI_BLOCK_FORMAT_LINEAR;
      cfg.base = base;
      cfg.row_stride = row_stride;
      SET_SURFACE_STRIDE(cfg, surf_stride);
   }
}

void
GENX(pan_emit_afbc_s_attachment)(const struct pan_fb_info *fb,
                                 unsigned layer_or_z_slice, void *payload)
{
   assert(PAN_ARCH >= 9);

#if PAN_ARCH >= 9
   const struct pan_image_view *s = fb->zs.view.s;
   const struct pan_image_plane_ref pref = pan_image_view_get_s_plane(s);
   uint64_t header, body_offset, hdr_row_stride;

   get_afbc_att_mem_props(pref, s->first_level, layer_or_z_slice, &header,
                          &body_offset, &hdr_row_stride);
   pan_cast_and_pack(payload, AFBC_S_TARGET, cfg) {
      cfg.msaa = mali_sampling_mode(s);
      cfg.write_format = translate_s_format(s->format);
      cfg.block_format = get_afbc_block_format(pref.image->props.modifier);
      cfg.header = header;
      cfg.body_offset = body_offset;
      cfg.header_row_stride = hdr_row_stride;
   }
#endif
}

void
GENX(pan_emit_u_tiled_s_attachment)(const struct pan_fb_info *fb,
                                    unsigned layer_or_z_slice, void *payload)
{
   const struct pan_image_view *s = fb->zs.view.s;
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_s_plane(s),
                                     s->first_level, layer_or_z_slice, &base,
                                     &row_stride, &surf_stride);
   pan_cast_and_pack(payload, S_TARGET, cfg) {
      cfg.msaa = mali_sampling_mode(s);
      cfg.write_format = translate_s_format(s->format);
      cfg.block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
      cfg.base = base;
      cfg.row_stride = row_stride;
      SET_SURFACE_STRIDE(cfg, surf_stride);
   }
}

void
GENX(pan_emit_linear_zs_attachment)(const struct pan_fb_info *fb,
                                    unsigned layer_or_z_slice, void *payload)
{
   const struct pan_image_view *zs = fb->zs.view.zs;
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_zs_plane(zs),
                                     zs->first_level, layer_or_z_slice, &base,
                                     &row_stride, &surf_stride);
   pan_cast_and_pack(payload, ZS_TARGET, cfg) {
      cfg.msaa = mali_sampling_mode(zs);
      cfg.write_format = translate_zs_format(zs->format);
      cfg.block_format = MALI_BLOCK_FORMAT_LINEAR;
      cfg.base = base;
      cfg.row_stride = row_stride;
      SET_SURFACE_STRIDE(cfg, surf_stride);
   }
}

void
GENX(pan_emit_u_tiled_zs_attachment)(const struct pan_fb_info *fb,
                                     unsigned layer_or_z_slice, void *payload)
{
   const struct pan_image_view *zs = fb->zs.view.zs;
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_zs_plane(zs),
                                     zs->first_level, layer_or_z_slice, &base,
                                     &row_stride, &surf_stride);
   pan_cast_and_pack(payload, ZS_TARGET, cfg) {
      cfg.msaa = mali_sampling_mode(zs);
      cfg.write_format = translate_zs_format(zs->format);
      cfg.block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
      cfg.base = base;
      cfg.row_stride = row_stride;
      SET_SURFACE_STRIDE(cfg, surf_stride);
   }
}

void
GENX(pan_emit_afbc_zs_attachment)(const struct pan_fb_info *fb,
                                  unsigned layer_or_z_slice, void *payload)
{
   const struct pan_image_view *zs = fb->zs.view.zs;
   const struct pan_image_plane_ref pref = pan_image_view_get_zs_plane(zs);
   uint64_t header, body_offset, hdr_row_stride;

   get_afbc_att_mem_props(pref, zs->first_level, layer_or_z_slice, &header,
                          &body_offset, &hdr_row_stride);

   pan_cast_and_pack(payload, AFBC_ZS_TARGET, cfg) {
      cfg.msaa = mali_sampling_mode(zs);
      cfg.write_format = translate_zs_format(zs->format);
      cfg.block_format = get_afbc_block_format(pref.image->props.modifier);

#if PAN_ARCH >= 9
      cfg.header = header;
      cfg.body_offset = body_offset;
      cfg.header_row_stride = hdr_row_stride;
#else
      cfg.header = header;
      cfg.body = header + body_offset;

#if PAN_ARCH >= 6
      cfg.header_row_stride =
         pan_afbc_stride_blocks(pref.image->props.format,
                                pref.image->props.modifier, hdr_row_stride);
#else
      cfg.body_size = 0x1000;
      cfg.chunk_size = 9;
      cfg.sparse = true;
#endif
#endif
   }
}

static void
pan_prepare_crc(const struct pan_fb_info *fb, int rt_crc,
                struct MALI_CRC *crc)
{
   if (rt_crc < 0)
      return;

   assert(rt_crc < fb->rt_count);

   const struct pan_image_view *rt = fb->rts[rt_crc].view;
   const struct pan_image_plane_ref pref = pan_image_view_get_color_plane(rt);
   const struct pan_image *image = pref.image;
   const struct pan_image_plane *plane = image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slice =
      &plane->layout.slices[rt->first_level];

   crc->base = plane->base + slice->crc.offset_B;
   crc->row_stride = slice->crc.stride_B;

#if PAN_ARCH >= 7
   crc->render_target = rt_crc;

   if (fb->rts[rt_crc].clear) {
      uint32_t clear_val = fb->rts[rt_crc].clear_value[0];
      crc->clear_color = clear_val | 0xc000000000000000 |
                         (((uint64_t)clear_val & 0xffff) << 32);
   }
#endif
}

static void
pan_emit_zs_crc_ext(const struct pan_fb_info *fb, unsigned layer_idx,
                    int rt_crc, struct mali_zs_crc_extension_packed *zs_crc_ext)
{
   struct mali_zs_crc_extension_packed desc;

   pan_pack(&desc, ZS_CRC_EXTENSION, cfg) {
      pan_prepare_crc(fb, rt_crc, &cfg.crc);
      cfg.zs.clean_pixel_write_enable = fb->zs.clear.z || fb->zs.clear.s;
   }

   if (fb->zs.view.zs) {
      const struct pan_image_plane_ref pref =
         pan_image_view_get_zs_plane(fb->zs.view.zs);
      const struct pan_mod_handler *mod_handler = pref.image->mod_handler;
      struct mali_zs_crc_extension_packed zs_part;

      mod_handler->emit_zs_attachment(
         fb, layer_idx + fb->zs.view.zs->first_layer, &zs_part);
      pan_merge(&desc, &zs_part, ZS_CRC_EXTENSION);
   }

   if (fb->zs.view.s) {
      const struct pan_image_plane_ref pref =
         pan_image_view_get_s_plane(fb->zs.view.s);
      const struct pan_mod_handler *mod_handler = pref.image->mod_handler;
      struct mali_zs_crc_extension_packed s_part;

      mod_handler->emit_s_attachment(fb, layer_idx + fb->zs.view.s->first_layer,
                                     &s_part);
      pan_merge(&desc, &s_part, ZS_CRC_EXTENSION);
   }

   *zs_crc_ext = desc;
}

/* Measure format as it appears in the tile buffer */

static unsigned
pan_bytes_per_pixel_tib(enum pipe_format format)
{
   const struct pan_blendable_format *bf =
      GENX(pan_blendable_format_from_pipe_format)(format);

   if (bf->internal) {
      /* Blendable formats are always 32-bits in the tile buffer,
       * extra bits are used as padding or to dither */
      return 4;
   } else {
      /* Non-blendable formats are raw, rounded up to the nearest
       * power-of-two size */
      unsigned bytes = util_format_get_blocksize(format);
      return util_next_power_of_two(bytes);
   }
}

static unsigned
pan_cbuf_bytes_per_pixel(const struct pan_fb_info *fb)
{
   /* dummy/non-existent render-targets use RGBA8 UNORM, e.g 4 bytes */
   const unsigned dummy_rt_size = 4 * fb->nr_samples;

   unsigned sum = 0;

   if (!fb->rt_count) {
      /* The HW needs at least one render-target */
      return dummy_rt_size;
   }

   for (int cb = 0; cb < fb->rt_count; ++cb) {
      unsigned rt_size = dummy_rt_size;
      const struct pan_image_view *rt = fb->rts[cb].view;
      if (rt)
         rt_size = pan_bytes_per_pixel_tib(rt->format) * rt->nr_samples;

      sum += rt_size;
   }

   if (fb->pls_enabled) {
      /* need at least 16 bytes per pixel for pixel local storage */
      sum = MAX2(sum, 16);
   }

   return sum;
}

static unsigned
pan_zsbuf_bytes_per_pixel(const struct pan_fb_info *fb)
{
   unsigned samples = fb->nr_samples;

   const struct pan_image_view *zs_view = fb->zs.view.zs;
   if (zs_view)
      samples = zs_view->nr_samples;

   const struct pan_image_view *s_view = fb->zs.view.s;
   if (s_view)
      samples = MAX2(samples, s_view->nr_samples);

   /* Depth is always stored in a 32-bit float. Stencil requires depth to
    * be allocated, but doesn't have it's own budget; it's tied to the
    * depth buffer.
    */
   return sizeof(float) * samples;
}

/*
 * Select the largest tile size that fits within the tilebuffer budget.
 * Formally, maximize (pixels per tile) such that it is a power of two and
 *
 *      (bytes per pixel) (pixels per tile) <= (max bytes per tile)
 *
 * A bit of algebra gives the following formula.
 *
 * Calculate the color buffer allocation size as well.
 */
void
GENX(pan_select_tile_size)(struct pan_fb_info *fb)
{
   unsigned bytes_per_pixel;

   assert(util_is_power_of_two_nonzero(fb->tile_buf_budget));
   assert(fb->tile_buf_budget >= 1024);

   bytes_per_pixel = pan_cbuf_bytes_per_pixel(fb);
   fb->tile_size = fb->tile_buf_budget >> util_logbase2_ceil(bytes_per_pixel);

   unsigned zs_bytes_per_pixel = pan_zsbuf_bytes_per_pixel(fb);
   if (zs_bytes_per_pixel > 0) {
      assert(util_is_power_of_two_nonzero(fb->z_tile_buf_budget));
      assert(fb->z_tile_buf_budget >= 1024);

      fb->tile_size =
         MIN2(fb->tile_size,
              fb->z_tile_buf_budget >> util_logbase2_ceil(zs_bytes_per_pixel));
   }

#if PAN_ARCH != 6
   /* Check if we're using too much tile-memory; if we are, try disabling
    * pipelining. This works because we're starting with an optimistic half
    * of the tile-budget, so we actually have another half that can be used.
    *
    * On v6 GPUs, doing this is not allowed; they *have* to pipeline.
    */
    if (fb->tile_size < 4 * 4)
       fb->tile_size *= 2;
#endif

   /* Clamp tile size to hardware limits */
   fb->tile_size = MIN2(fb->tile_size, pan_max_effective_tile_size(PAN_ARCH));
   assert(fb->tile_size >= 4 * 4);

   /* Colour buffer allocations must be 1K aligned. */
   fb->cbuf_allocation = ALIGN_POT(bytes_per_pixel * fb->tile_size, 1024);
#if PAN_ARCH == 6
   assert(fb->cbuf_allocation <= fb->tile_buf_budget && "tile too big");
#else
   assert(fb->cbuf_allocation <= fb->tile_buf_budget * 2 && "tile too big");
#endif
}

static enum mali_color_format
pan_mfbd_raw_format(unsigned bits)
{
   /* clang-format off */
   switch (bits) {
   case    8: return MALI_COLOR_FORMAT_RAW8;
   case   16: return MALI_COLOR_FORMAT_RAW16;
   case   24: return MALI_COLOR_FORMAT_RAW24;
   case   32: return MALI_COLOR_FORMAT_RAW32;
   case   48: return MALI_COLOR_FORMAT_RAW48;
   case   64: return MALI_COLOR_FORMAT_RAW64;
   case   96: return MALI_COLOR_FORMAT_RAW96;
   case  128: return MALI_COLOR_FORMAT_RAW128;
   case  192: return MALI_COLOR_FORMAT_RAW192;
   case  256: return MALI_COLOR_FORMAT_RAW256;
   case  384: return MALI_COLOR_FORMAT_RAW384;
   case  512: return MALI_COLOR_FORMAT_RAW512;
   case  768: return MALI_COLOR_FORMAT_RAW768;
   case 1024: return MALI_COLOR_FORMAT_RAW1024;
   case 1536: return MALI_COLOR_FORMAT_RAW1536;
   case 2048: return MALI_COLOR_FORMAT_RAW2048;
   default: UNREACHABLE("invalid raw bpp");
   }
   /* clang-format on */
}

static void
get_rt_formats(enum pipe_format pfmt, uint32_t *writeback, uint32_t *internal,
               uint32_t *pswizzle)
{
   /* Explode details on the format */
   const struct util_format_description *desc = util_format_description(pfmt);

   /* The swizzle for rendering is inverted from texturing */
   unsigned char swizzle[4] = {
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_Y,
      PIPE_SWIZZLE_Z,
      PIPE_SWIZZLE_W,
   };

   const struct pan_blendable_format *bfmt =
      GENX(pan_blendable_format_from_pipe_format)(pfmt);

   if (bfmt->internal) {
      *internal = bfmt->internal;
      *writeback = bfmt->writeback;
      pan_invert_swizzle(desc->swizzle, swizzle);
   } else {
      /* Construct RAW internal/writeback, where internal is
       * specified logarithmically (round to next power-of-two).
       * Offset specified from RAW8, where 8 = 2^3 */

      unsigned bits = desc->block.bits;
      assert(bits >= 8 && bits <= 128);
      unsigned offset = util_logbase2_ceil(bits) - 3;
      assert(offset <= 4);

      *internal = MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW8 + offset;
      *writeback = pan_mfbd_raw_format(bits);
   }

   *pswizzle = pan_translate_swizzle_4(swizzle);
}

/* forward declaration */
static bool pan_force_clean_write_on(const struct pan_image *img, unsigned tile_size);

static struct MALI_RT_CLEAR
rt_clear(const struct pan_fb_color_attachment *rt)
{
   if (!rt || !rt->clear)
      return (struct MALI_RT_CLEAR){0};

   return (struct MALI_RT_CLEAR){
      .color_0 = rt->clear_value[0],
      .color_1 = rt->clear_value[1],
      .color_2 = rt->clear_value[2],
      .color_3 = rt->clear_value[3],
   };
}

static bool
rt_clean_pixel_write(const struct pan_fb_color_attachment *rt,
                     unsigned tile_size)
{
   if (rt->clear)
      return true;

#if PAN_ARCH >= 6
   const struct pan_image_plane_ref pref =
      pan_image_view_get_color_plane(rt->view);

   if (pan_force_clean_write_on(pref.image, tile_size))
      return true;
#endif

   return false;
}

#define rt_common_cfg(rt__, cbuf_offset__, tile_size__, cfg__)                 \
   do {                                                                        \
      assert((rt__)->view != NULL);                                            \
      (cfg__).clean_pixel_write_enable =                                       \
         rt_clean_pixel_write(rt__, tile_size__);                              \
      (cfg__).internal_buffer_offset = cbuf_offset__;                          \
      (cfg__).clear = rt_clear(rt__);                                          \
      (cfg__).dithering_enable = true;                                         \
      (cfg__).writeback_msaa = mali_sampling_mode((rt__)->view);               \
   } while (0)

void
GENX(pan_emit_afbc_color_attachment)(const struct pan_fb_info *fb,
                                     unsigned rt_idx,
                                     unsigned layer_or_z_slice,
                                     unsigned cbuf_offset, void *payload)
{
   const struct pan_fb_color_attachment *rt = &fb->rts[rt_idx];
   const struct pan_image_view *iview = rt->view;
   const struct pan_image_plane_ref pref = pan_image_view_get_color_plane(iview);
   const struct pan_image *image = pref.image;
   uint64_t header, body_offset, hdr_row_stride;

   get_afbc_att_mem_props(pref, iview->first_level, layer_or_z_slice, &header,
                          &body_offset, &hdr_row_stride);

   /* TODO: YUV RT. */
   assert(!pan_format_is_yuv(iview->format));
   pan_cast_and_pack(payload, AFBC_RGB_RENDER_TARGET, cfg) {
      rt_common_cfg(rt, cbuf_offset, fb->tile_size, cfg);
      cfg.write_enable = true;
      get_rt_formats(iview->format, &cfg.writeback_format, &cfg.internal_format,
                     &cfg.swizzle);

      /* AFBC+RAW24 is not supported on v9+. Use RAW32 instead, and let the AFBC
       * compression mode select the actual size.
       */
      if (PAN_ARCH >= 9 && cfg.writeback_format == MALI_COLOR_FORMAT_RAW24)
         cfg.writeback_format = MALI_COLOR_FORMAT_RAW32;

      cfg.srgb = util_format_is_srgb(iview->format);
      cfg.writeback_block_format = get_afbc_block_format(image->props.modifier);
      cfg.yuv_transform = image->props.modifier & AFBC_FORMAT_MOD_YTR;
#if PAN_ARCH >= 6
      cfg.wide_block = pan_afbc_is_wide(image->props.modifier);
      cfg.split_block = (image->props.modifier & AFBC_FORMAT_MOD_SPLIT);
#endif

#if PAN_ARCH >= 9
      cfg.header = header;
      cfg.body_offset = body_offset;
      cfg.row_stride = hdr_row_stride;
      cfg.compression_mode = pan_afbc_compression_mode(
         image->planes[pref.plane_idx]->layout.afbc.mode);
#else
      cfg.header = header;
      cfg.body = header + body_offset;

#if PAN_ARCH >= 6
      cfg.row_stride =
         pan_afbc_stride_blocks(image->props.format,
                                image->props.modifier, hdr_row_stride);
#else
      const struct pan_image_plane *plane = image->planes[pref.plane_idx];
      const struct pan_image_slice_layout *slayout =
         &plane->layout.slices[iview->first_level];

      cfg.body_size = slayout->afbc.surface_stride_B -
                      pan_afbc_body_offset(PAN_ARCH, image->props.modifier,
                                           slayout->afbc.header.surface_size_B);
      cfg.chunk_size = 9;
      cfg.sparse = true;
#endif
#endif
   }
}

void
GENX(pan_emit_u_tiled_color_attachment)(const struct pan_fb_info *fb,
                                        unsigned rt_idx,
                                        unsigned layer_or_z_slice,
                                        unsigned cbuf_offset, void *payload)
{
   const struct pan_fb_color_attachment *rt = &fb->rts[rt_idx];
   const struct pan_image_view *iview = rt->view;
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_color_plane(iview),
                                     iview->first_level, layer_or_z_slice,
                                     &base, &row_stride, &surf_stride);

   /* TODO: YUV RT. */
   assert(!pan_format_is_yuv(iview->format));
   pan_cast_and_pack(payload, RGB_RENDER_TARGET, cfg) {
      rt_common_cfg(rt, cbuf_offset, fb->tile_size, cfg);
      cfg.write_enable = true;
      cfg.writeback_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
      get_rt_formats(iview->format, &cfg.writeback_format, &cfg.internal_format,
                     &cfg.swizzle);
      cfg.srgb = util_format_is_srgb(iview->format);
      cfg.writeback_buffer.base = base;
      cfg.writeback_buffer.row_stride = row_stride;
      cfg.writeback_buffer.surface_stride = surf_stride;
   }
}

void
GENX(pan_emit_linear_color_attachment)(const struct pan_fb_info *fb,
                                       unsigned rt_idx,
                                       unsigned layer_or_z_slice,
                                       unsigned cbuf_offset, void *payload)
{
   const struct pan_fb_color_attachment *rt = &fb->rts[rt_idx];
   const struct pan_image_view *iview = rt->view;
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_color_plane(iview),
                                     iview->first_level, layer_or_z_slice,
                                     &base, &row_stride, &surf_stride);

   /* TODO: YUV RT. */
   assert(!pan_format_is_yuv(iview->format));
   pan_cast_and_pack(payload, RGB_RENDER_TARGET, cfg) {
      rt_common_cfg(rt, cbuf_offset, fb->tile_size, cfg);
      cfg.write_enable = true;
      cfg.writeback_block_format = MALI_BLOCK_FORMAT_LINEAR;
      get_rt_formats(iview->format, &cfg.writeback_format, &cfg.internal_format,
                     &cfg.swizzle);
      cfg.srgb = util_format_is_srgb(iview->format);
      cfg.writeback_buffer.base = base;
      cfg.writeback_buffer.row_stride = row_stride;
      cfg.writeback_buffer.surface_stride = surf_stride;
   }
}

#if PAN_ARCH >= 10
void
GENX(pan_emit_afrc_color_attachment)(const struct pan_fb_info *fb,
                                     unsigned rt_idx, unsigned layer_or_z_slice,
                                     unsigned cbuf_offset, void *payload)
{
   const struct pan_fb_color_attachment *rt = &fb->rts[rt_idx];
   const struct pan_image_view *iview = rt->view;
   const struct pan_image_plane_ref pref = pan_image_view_get_color_plane(iview);
   const struct pan_image *image = pref.image;
   struct pan_afrc_format_info finfo =
      pan_afrc_get_format_info(image->props.format);
   uint64_t base, row_stride, surf_stride;

   get_tiled_or_linear_att_mem_props(pan_image_view_get_s_plane(iview),
                                     iview->first_level, layer_or_z_slice,
                                     &base, &row_stride, &surf_stride);

   /* TODO: YUV RT. */
   assert(!pan_format_is_yuv(iview->format));
   pan_cast_and_pack(payload, AFRC_RGB_RENDER_TARGET, cfg) {
      rt_common_cfg(rt, cbuf_offset, fb->tile_size, cfg);
      cfg.writeback_mode = MALI_WRITEBACK_MODE_AFRC_RGB;
      cfg.afrc_block_size = pan_afrc_block_size(image->props.modifier, 0);
      cfg.afrc_format = pan_afrc_format(finfo, image->props.modifier, 0);
      get_rt_formats(iview->format, &cfg.writeback_format, &cfg.internal_format,
                     &cfg.swizzle);
      cfg.writeback_buffer.base = base;
      cfg.writeback_buffer.row_stride = row_stride;
      cfg.writeback_buffer.surface_stride = surf_stride;
   }
}
#endif
#endif

void
GENX(pan_emit_tls)(const struct pan_tls_info *info,
                   struct mali_local_storage_packed *out)
{
   pan_pack(out, LOCAL_STORAGE, cfg) {
      if (info->tls.size) {
         unsigned shift = pan_get_stack_shift(info->tls.size);

         cfg.tls_size = shift;
#if PAN_ARCH >= 9
         /* For now, always use packed TLS addressing. This is
          * better for the cache and requires no fix up code in
          * the shader. We may need to revisit this someday for
          * OpenCL generic pointer support.
          */
         cfg.tls_address_mode = MALI_ADDRESS_MODE_PACKED;

         assert((info->tls.ptr & 4095) == 0);
         cfg.tls_base_pointer = info->tls.ptr >> 8;
#else
         cfg.tls_base_pointer = info->tls.ptr;
#endif
      }

      if (info->wls.size) {
         assert(!(info->wls.ptr & 4095));
         assert((info->wls.ptr & 0xffffffff00000000ULL) ==
                ((info->wls.ptr + info->wls.size - 1) & 0xffffffff00000000ULL));
         cfg.wls_base_pointer = info->wls.ptr;
         unsigned wls_size = pan_wls_adjust_size(info->wls.size);
         cfg.wls_instances = info->wls.instances;
         cfg.wls_size_scale = util_logbase2(wls_size) + 1;
      } else {
         cfg.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
      }
   }
}

#if PAN_ARCH <= 5
static void
pan_emit_midgard_tiler(const struct pan_fb_info *fb,
                       const struct pan_tiler_context *tiler_ctx,
                       struct mali_tiler_context_packed *out)
{
   bool hierarchy = !tiler_ctx->midgard.no_hierarchical_tiling;

   assert(tiler_ctx->midgard.polygon_list);

   pan_pack(out, TILER_CONTEXT, cfg) {
      unsigned header_size;

      if (tiler_ctx->midgard.disable) {
         cfg.hierarchy_mask =
            hierarchy ? MALI_MIDGARD_TILER_DISABLED : MALI_MIDGARD_TILER_USER;
         header_size = MALI_MIDGARD_TILER_MINIMUM_HEADER_SIZE;
         cfg.polygon_list_size = header_size + (hierarchy ? 0 : 4);
         cfg.heap_start = tiler_ctx->midgard.polygon_list;
         cfg.heap_end = tiler_ctx->midgard.polygon_list;
      } else {
         cfg.hierarchy_mask = pan_choose_hierarchy_mask(
            fb->width, fb->height, tiler_ctx->midgard.vertex_count, hierarchy);
         header_size = pan_tiler_header_size(fb->width, fb->height,
                                             cfg.hierarchy_mask, hierarchy);
         cfg.polygon_list_size = pan_tiler_full_size(
            fb->width, fb->height, cfg.hierarchy_mask, hierarchy);
         cfg.heap_start = tiler_ctx->midgard.heap.start;
         cfg.heap_end = cfg.heap_start + tiler_ctx->midgard.heap.size;
      }

      cfg.polygon_list = tiler_ctx->midgard.polygon_list;
      cfg.polygon_list_body = cfg.polygon_list + header_size;
   }
}
#endif

#if PAN_ARCH >= 5
static void
pan_emit_rt(const struct pan_fb_info *fb, unsigned layer_idx, unsigned idx,
            unsigned cbuf_offset, struct mali_render_target_packed *out)
{
   const struct pan_image_view *rt = fb->rts[idx].view;

   if (!rt || fb->rts[idx].discard) {
      pan_cast_and_pack(out, RGB_RENDER_TARGET, cfg) {
         cfg.clean_pixel_write_enable = fb->rts[idx].clear;
         cfg.internal_buffer_offset = cbuf_offset;
         cfg.clear = rt_clear(&fb->rts[idx]);
         cfg.dithering_enable = true;
         cfg.internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;
#if PAN_ARCH >= 7
         cfg.writeback_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
#endif
      }

      return;
   }

   struct pan_image_plane_ref pref = pan_image_view_get_color_plane(rt);
   assert(pref.image);
   const struct pan_mod_handler *mod_handler = pref.image->mod_handler;
   assert(mod_handler);

   ASSERTED unsigned layer_count = rt->dim == MALI_TEXTURE_DIMENSION_3D
                                      ? pref.image->props.extent_px.depth
                                      : rt->last_layer - rt->first_layer + 1;

   assert(rt->last_level == rt->first_level);
   assert(layer_idx < layer_count);

   mod_handler->emit_color_attachment(fb, idx, layer_idx + rt->first_layer,
                                      cbuf_offset, out);
}

#if PAN_ARCH >= 6
/* All Bifrost and Valhall GPUs are affected by issue TSIX-2033:
 *
 *      Forcing clean_tile_writes breaks INTERSECT readbacks
 *
 * To workaround, use the frame shader mode ALWAYS instead of INTERSECT if
 * clean tile writes is forced. Since INTERSECT is a hint that the hardware may
 * ignore, this cannot affect correctness, only performance */

static enum mali_pre_post_frame_shader_mode
pan_fix_frame_shader_mode(enum mali_pre_post_frame_shader_mode mode,
                          bool force_clean_tile)
{
   if (force_clean_tile && mode == MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT)
      return MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS;
   else
      return mode;
}

/* Regardless of clean_tile_write_enable, the hardware writes clean tiles if
 * the effective tile size differs from the superblock size of any enabled AFBC
 * render target. Check this condition. */

static bool
pan_force_clean_write_on(const struct pan_image *image, unsigned tile_size)
{
   if (!image)
      return false;

   if (!drm_is_afbc(image->props.modifier))
      return false;

   struct pan_image_block_size renderblk_sz =
      pan_afbc_renderblock_size(image->props.modifier);

   assert(renderblk_sz.width >= 16 && renderblk_sz.height >= 16);
   assert(tile_size <= pan_max_effective_tile_size(PAN_ARCH));

   return tile_size != renderblk_sz.width * renderblk_sz.height;
}

static bool
pan_force_clean_write(const struct pan_fb_info *fb, unsigned tile_size)
{
   /* Maximum tile size */
   assert(tile_size <= pan_max_effective_tile_size(PAN_ARCH));

   for (unsigned i = 0; i < fb->rt_count; ++i) {
      if (!fb->rts[i].view || fb->rts[i].discard)
         continue;

      const struct pan_image_plane_ref pref =
         pan_image_view_get_color_plane(fb->rts[i].view);
      const struct pan_image *img = pref.image;

      if (pan_force_clean_write_on(img, tile_size))
         return true;
   }

   if (fb->zs.view.zs && !fb->zs.discard.z &&
       pan_force_clean_write_on(
          pan_image_view_get_zs_plane(fb->zs.view.zs).image, tile_size))
      return true;

   if (fb->zs.view.s && !fb->zs.discard.s &&
       pan_force_clean_write_on(pan_image_view_get_s_plane(fb->zs.view.s).image,
                                tile_size))
      return true;

   return false;
}

#endif

static void
check_fb_attachments(const struct pan_fb_info *fb)
{
#ifndef NDEBUG
   for (unsigned i = 0; i < fb->rt_count; i++) {
      if (fb->rts[i].view) {
         pan_image_view_check(fb->rts[i].view);
         assert(fb->rts[i].view->nr_samples == fb->nr_samples);
      }
   }

   if (fb->zs.view.zs) {
      pan_image_view_check(fb->zs.view.zs);
      assert(fb->zs.view.zs->nr_samples == fb->nr_samples);
   }
   if (fb->zs.view.s)
      pan_image_view_check(fb->zs.view.s);
#endif
}

unsigned
GENX(pan_emit_fbd)(const struct pan_fb_info *fb, unsigned layer_idx,
                   const struct pan_tls_info *tls,
                   const struct pan_tiler_context *tiler_ctx, void *out)
{
   check_fb_attachments(fb);

   void *fbd = out;
   void *rtd = out + pan_size(FRAMEBUFFER);

#if PAN_ARCH <= 5
   GENX(pan_emit_tls)(tls, pan_section_ptr(fbd, FRAMEBUFFER, LOCAL_STORAGE));
#endif

   int crc_rt = GENX(pan_select_crc_rt)(fb, fb->tile_size);
   bool has_zs_crc_ext = (fb->zs.view.zs || fb->zs.view.s || crc_rt >= 0);

   pan_section_pack(fbd, FRAMEBUFFER, PARAMETERS, cfg) {
#if PAN_ARCH >= 6
      bool force_clean_write = pan_force_clean_write(fb, fb->tile_size);

      cfg.sample_locations = fb->sample_positions;
      cfg.pre_frame_0 = pan_fix_frame_shader_mode(fb->bifrost.pre_post.modes[0],
                                                  force_clean_write);
      cfg.pre_frame_1 = pan_fix_frame_shader_mode(fb->bifrost.pre_post.modes[1],
                                                  force_clean_write);
      cfg.post_frame = pan_fix_frame_shader_mode(fb->bifrost.pre_post.modes[2],
                                                 force_clean_write);
#if PAN_ARCH < 9
      /* On Bifrost, the layer_id is passed through a push_uniform, which forces
       * us to have one pre/post DCD array per layer. */
      cfg.frame_shader_dcds =
         fb->bifrost.pre_post.dcds.gpu + (layer_idx * 3 * pan_size(DRAW));
#else
      /* On Valhall, layer_id is passed through the framebuffer frame_arg, which
       * is preloaded in r62, so we can use the same pre/post DCD array for all
       * layers. */
      cfg.frame_shader_dcds = fb->bifrost.pre_post.dcds.gpu;
#endif
      cfg.tiler =
         PAN_ARCH >= 9 ? tiler_ctx->valhall.desc : tiler_ctx->bifrost.desc;
#endif
      cfg.width = fb->width;
      cfg.height = fb->height;
      cfg.bound_min_x = fb->frame_bounding_box.minx;
      cfg.bound_min_y = fb->frame_bounding_box.miny;
      cfg.bound_max_x = fb->frame_bounding_box.maxx;
      cfg.bound_max_y = fb->frame_bounding_box.maxy;
      cfg.effective_tile_size = fb->tile_size;
      /* Ensure we cover the samples on the edge for 16x MSAA */
      cfg.tie_break_rule = fb->nr_samples == 16 ?
         MALI_TIE_BREAK_RULE_MINUS_180_OUT_0_IN :
         MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
      cfg.render_target_count = MAX2(fb->rt_count, 1);

      /* Default to 24 bit depth if there's no surface. */
      cfg.z_internal_format =
         fb->zs.view.zs ? pan_get_z_internal_format(fb->zs.view.zs->format)
                        : MALI_Z_INTERNAL_FORMAT_D24;

      cfg.z_clear = fb->zs.clear_value.depth;
      cfg.s_clear = fb->zs.clear_value.stencil;
      cfg.color_buffer_allocation = fb->cbuf_allocation;

      /* The force_samples setting dictates the sample-count that is used
       * for rasterization, and works like D3D11's ForcedSampleCount feature:
       *
       * - If force_samples == 0: Let nr_samples dictate sample count
       * - If force_samples == 1: force single-sampled rasterization
       * - If force_samples >= 1: force multi-sampled rasterization
       *
       * This can be used to read SYSTEM_VALUE_SAMPLE_MASK_IN from the
       * fragment shader, even when performing single-sampled rendering.
       */
      if (fb->pls_enabled) {
         cfg.sample_count = 4;
         cfg.sample_pattern = pan_sample_pattern(1);
      } else if (!fb->force_samples) {
         cfg.sample_count = fb->nr_samples;
         cfg.sample_pattern = pan_sample_pattern(fb->nr_samples);
      } else if (fb->force_samples == 1) {
         cfg.sample_count = fb->nr_samples;
         cfg.sample_pattern = pan_sample_pattern(1);
      } else {
         cfg.sample_count = 1;
         cfg.sample_pattern = pan_sample_pattern(fb->force_samples);
      }

      cfg.z_write_enable = (fb->zs.view.zs && !fb->zs.discard.z);
      cfg.s_write_enable = (fb->zs.view.s && !fb->zs.discard.s);
      cfg.has_zs_crc_extension = has_zs_crc_ext;

      if (crc_rt >= 0) {
         bool *valid = fb->rts[crc_rt].crc_valid;
         bool full = !fb->draw_extent.minx && !fb->draw_extent.miny &&
                     fb->draw_extent.maxx == (fb->width - 1) &&
                     fb->draw_extent.maxy == (fb->height - 1);
         bool clean_tile_write = fb->rts[crc_rt].clear;

#if PAN_ARCH >= 6
         clean_tile_write |= pan_force_clean_write_on(
            pan_image_view_get_color_plane(fb->rts[crc_rt].view).image,
            fb->tile_size);
#endif

         /* If the CRC was valid it stays valid, if it wasn't, we must ensure
          * the render operation covers the full frame, and clean tiles are
          * pushed to memory. */
         bool new_valid = *valid | (full && clean_tile_write);

         cfg.crc_read_enable = *valid;

         /* If the data is currently invalid, still write CRC
          * data if we are doing a full write, so that it is
          * valid for next time. */
         cfg.crc_write_enable = new_valid;

         *valid = new_valid;
      }

#if PAN_ARCH >= 9
      cfg.point_sprite_coord_origin_max_y = fb->sprite_coord_origin;
      cfg.first_provoking_vertex = fb->first_provoking_vertex;

      /* internal_layer_index is used to select the right primitive list in the
       * tiler context, and frame_arg is the value that's passed to the fragment
       * shader through r62-r63, which we use to pass gl_Layer. Since the
       * layer_idx only takes 8-bits, we might use the extra 56-bits we have
       * in frame_argument to pass other information to the fragment shader at
       * some point. */
      assert(layer_idx >= tiler_ctx->valhall.layer_offset);
      cfg.internal_layer_index = layer_idx - tiler_ctx->valhall.layer_offset;
      cfg.frame_argument = layer_idx;
#endif
   }

#if PAN_ARCH >= 6
   pan_section_pack(fbd, FRAMEBUFFER, PADDING, padding)
      ;
#else
   pan_emit_midgard_tiler(fb, tiler_ctx,
                          pan_section_ptr(fbd, FRAMEBUFFER, TILER));

   /* All weights set to 0, nothing to do here */
   pan_section_pack(fbd, FRAMEBUFFER, TILER_WEIGHTS, w)
      ;
#endif

   if (has_zs_crc_ext) {
      struct mali_zs_crc_extension_packed *zs_crc_ext =
         out + pan_size(FRAMEBUFFER);

      pan_emit_zs_crc_ext(fb, layer_idx, crc_rt, zs_crc_ext);
      rtd += pan_size(ZS_CRC_EXTENSION);
   }

   unsigned rt_count = MAX2(fb->rt_count, 1);
   unsigned cbuf_offset = 0;
   for (unsigned i = 0; i < rt_count; i++) {
      pan_emit_rt(fb, layer_idx, i, cbuf_offset, rtd);
      rtd += pan_size(RENDER_TARGET);
      if (!fb->rts[i].view)
         continue;

      cbuf_offset += pan_bytes_per_pixel_tib(fb->rts[i].view->format) *
                     fb->tile_size *
                     pan_image_view_get_nr_samples(fb->rts[i].view);

      if (i != crc_rt)
         *(fb->rts[i].crc_valid) = false;
   }

   struct mali_framebuffer_pointer_packed tag;
   pan_pack(&tag, FRAMEBUFFER_POINTER, cfg) {
      cfg.zs_crc_extension_present = has_zs_crc_ext;
      cfg.render_target_count = MAX2(fb->rt_count, 1);
   }
   return tag.opaque[0];
}
#else /* PAN_ARCH == 4 */
static enum mali_color_format
pan_sfbd_raw_format(unsigned bits)
{
   /* clang-format off */
   switch (bits) {
   case   16: return MALI_COLOR_FORMAT_1_16B_CHANNEL;
   case   32: return MALI_COLOR_FORMAT_1_32B_CHANNEL;
   case   48: return MALI_COLOR_FORMAT_3_16B_CHANNELS;
   case   64: return MALI_COLOR_FORMAT_2_32B_CHANNELS;
   case   96: return MALI_COLOR_FORMAT_3_32B_CHANNELS;
   case  128: return MALI_COLOR_FORMAT_4_32B_CHANNELS;
   default: UNREACHABLE("invalid raw bpp");
   }
   /* clang-format on */
}

void
GENX(pan_select_tile_size)(struct pan_fb_info *fb)
{
   /* Tile size and color buffer allocation are not configurable on gen 4 */
   fb->tile_size = 16 * 16;
}

unsigned
GENX(pan_emit_fbd)(const struct pan_fb_info *fb, unsigned layer_idx,
                   const struct pan_tls_info *tls,
                   const struct pan_tiler_context *tiler_ctx, void *fbd)
{
   assert(fb->rt_count <= 1);

   GENX(pan_emit_tls)(tls, pan_section_ptr(fbd, FRAMEBUFFER, LOCAL_STORAGE));
   pan_section_pack(fbd, FRAMEBUFFER, PARAMETERS, cfg) {
      cfg.bound_max_x = fb->width - 1;
      cfg.bound_max_y = fb->height - 1;
      cfg.dithering_enable = true;
      cfg.clean_pixel_write_enable = true;
      cfg.tie_break_rule = MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
      if (fb->rts[0].clear) {
         cfg.clear_color_0 = fb->rts[0].clear_value[0];
         cfg.clear_color_1 = fb->rts[0].clear_value[1];
         cfg.clear_color_2 = fb->rts[0].clear_value[2];
         cfg.clear_color_3 = fb->rts[0].clear_value[3];
      }

      if (fb->zs.clear.z)
         cfg.z_clear = fb->zs.clear_value.depth;

      if (fb->zs.clear.s)
         cfg.s_clear = fb->zs.clear_value.stencil;

      if (fb->rt_count && fb->rts[0].view) {
         const struct pan_image_view *rt = fb->rts[0].view;
         const struct pan_image_plane_ref pref =
            pan_image_view_get_color_plane(rt);
         const struct pan_image *image = pref.image;
         const struct pan_image_plane *plane = image->planes[pref.plane_idx];
         const struct pan_image_slice_layout *slayout =
            &plane->layout.slices[rt->first_level];
         const unsigned array_idx =
            image->props.dim == MALI_TEXTURE_DIMENSION_3D ? 0 : rt->first_layer;
         const unsigned surf_idx =
            image->props.dim == MALI_TEXTURE_DIMENSION_3D ? rt->first_layer : 0;

         const struct util_format_description *desc =
            util_format_description(rt->format);

         /* The swizzle for rendering is inverted from texturing */
         unsigned char swizzle[4];
         pan_invert_swizzle(desc->swizzle, swizzle);
         cfg.swizzle = pan_translate_swizzle_4(swizzle);

         struct pan_blendable_format fmt =
            *GENX(pan_blendable_format_from_pipe_format)(rt->format);

         if (fmt.internal) {
            cfg.internal_format = fmt.internal;
            cfg.color_writeback_format = fmt.writeback;
         } else {
            /* Construct RAW internal/writeback */
            unsigned bits = desc->block.bits;

            cfg.internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW_VALUE;
            cfg.color_writeback_format = pan_sfbd_raw_format(bits);
         }

         cfg.color_write_enable = !fb->rts[0].discard;
         cfg.color_writeback.base =
            plane->base + slayout->offset_B +
            (array_idx * plane->layout.array_stride_B) +
            (surf_idx * slayout->tiled_or_linear.surface_stride_B);
         cfg.color_writeback.row_stride = slayout->tiled_or_linear.row_stride_B;

         assert(image->props.modifier == DRM_FORMAT_MOD_LINEAR ||
                image->props.modifier ==
                   DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);
         cfg.color_block_format =
            image->props.modifier ==
                  DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED
               ? MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED
               : MALI_BLOCK_FORMAT_LINEAR;

         if (pan_image_view_has_crc(rt)) {
            cfg.crc_buffer.row_stride = slayout->crc.stride_B;
            cfg.crc_buffer.base = plane->base + slayout->crc.offset_B;
         }
      }

      if (fb->zs.view.zs) {
         const struct pan_image_view *zs = fb->zs.view.zs;
         const struct pan_image_plane_ref pref =
            pan_image_view_get_zs_plane(zs);
         const struct pan_image *image = pref.image;
         const struct pan_image_plane *plane = image->planes[pref.plane_idx];
         const struct pan_image_slice_layout *slayout =
            &plane->layout.slices[zs->first_level];
         const unsigned array_idx =
            image->props.dim == MALI_TEXTURE_DIMENSION_3D ? 0 : zs->first_layer;
         const unsigned surf_idx =
            image->props.dim == MALI_TEXTURE_DIMENSION_3D ? zs->first_layer : 0;

         cfg.zs_write_enable = !fb->zs.discard.z;
         cfg.zs_writeback.base =
            plane->base + slayout->offset_B +
            (array_idx * plane->layout.array_stride_B) +
            (surf_idx * slayout->tiled_or_linear.surface_stride_B);
         cfg.zs_writeback.row_stride = slayout->tiled_or_linear.row_stride_B;
         assert(image->props.modifier == DRM_FORMAT_MOD_LINEAR ||
                image->props.modifier ==
                   DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);
         cfg.zs_block_format =
            image->props.modifier ==
                  DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED
               ? MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED
               : MALI_BLOCK_FORMAT_LINEAR;

         cfg.zs_format = translate_zs_format(zs->format);
      }

      cfg.sample_count = fb->nr_samples;

      if (fb->rt_count)
         cfg.msaa = mali_sampling_mode(fb->rts[0].view);
   }

   pan_emit_midgard_tiler(fb, tiler_ctx,
                          pan_section_ptr(fbd, FRAMEBUFFER, TILER));

   /* All weights set to 0, nothing to do here */
   pan_section_pack(fbd, FRAMEBUFFER, TILER_WEIGHTS, w)
      ;

   pan_section_pack(fbd, FRAMEBUFFER, PADDING_1, padding)
      ;
   pan_section_pack(fbd, FRAMEBUFFER, PADDING_2, padding)
      ;
   return 0;
}
#endif

#if PAN_ARCH <= 9
void
GENX(pan_emit_fragment_job_payload)(const struct pan_fb_info *fb, uint64_t fbd,
                                    void *out)
{
   pan_section_pack(out, FRAGMENT_JOB, PAYLOAD, payload) {
      payload.bound_min_x = fb->draw_extent.minx >> MALI_TILE_SHIFT;
      payload.bound_min_y = fb->draw_extent.miny >> MALI_TILE_SHIFT;
      payload.bound_max_x = fb->draw_extent.maxx >> MALI_TILE_SHIFT;
      payload.bound_max_y = fb->draw_extent.maxy >> MALI_TILE_SHIFT;
      payload.framebuffer = fbd;

#if PAN_ARCH >= 5
      if (fb->tile_map.base) {
         payload.has_tile_enable_map = true;
         payload.tile_enable_map = fb->tile_map.base;
         payload.tile_enable_map_row_stride = fb->tile_map.stride;
      }
#endif
   }
}
#endif

#if PAN_ARCH >= 6
static uint32_t
pan_calc_bins_pointer_size(uint32_t width, uint32_t height, uint32_t tile_size,
                           uint32_t hierarchy_mask)
{
   const uint32_t bin_ptr_size = PAN_ARCH >= 12 ? 16 : 8;

   uint32_t bins_x[PAN_BIN_LEVEL_COUNT];
   uint32_t bins_y[PAN_BIN_LEVEL_COUNT];
   uint32_t bins[PAN_BIN_LEVEL_COUNT];
   uint32_t bins_enabled;

   /* On v12+, hierarchy_mask is only used if 4 levels are used at most,
    * otherwise it selects another mask (0xAC with a tile_size greater than
    * 32x32, 0xAC with 32x32 and lower) */
   if ((hierarchy_mask == 0 || util_bitcount(hierarchy_mask) > 4) &&
       PAN_ARCH >= 12) {
      if (tile_size > 32 * 32)
         hierarchy_mask = 0xAC;
      else
         hierarchy_mask = 0xAA;
   }

   bins_x[0] = DIV_ROUND_UP(width, 16);
   bins_y[0] = DIV_ROUND_UP(height, 16);
   bins[0] = bins_x[0] * bins_y[0];

   for (uint32_t i = 1; i < ARRAY_SIZE(bins); i++) {
      bins_x[i] = DIV_ROUND_UP(bins_x[i - 1], 2);
      bins_y[i] = DIV_ROUND_UP(bins_y[i - 1], 2);
      bins[i] = bins_x[i] * bins_y[i];
   }

   bins_enabled = 0;
   for (uint32_t i = 0; i < ARRAY_SIZE(bins); i++) {
      if ((hierarchy_mask & (1 << i)) != 0)
         bins_enabled += bins[i];
   }

   return DIV_ROUND_UP(bins_enabled, 8) * 8 * bin_ptr_size;
}

unsigned
GENX(pan_select_tiler_hierarchy_mask)(unsigned width, unsigned height,
                                      unsigned max_levels, unsigned tile_size,
                                      unsigned mem_budget)
{
   /* On v12+, the hierarchy_mask is deprecated and letting the hardware decide
    * is prefered. We attempt to use hierarchy_mask of 0 in case the bins can
    * fit in our memory budget.
    */
   if (PAN_ARCH >= 12 &&
       pan_calc_bins_pointer_size(width, height, tile_size, 0) <= mem_budget)
      return 0;

   uint32_t max_fb_wh = MAX2(width, height);
   uint32_t last_hierarchy_bit = util_last_bit(DIV_ROUND_UP(max_fb_wh, 16));
   uint32_t hierarchy_mask;

   if (max_levels < 8) {
      /* spread the bits out somewhat */
      static uint32_t default_mask[] = {
         0, 0x80, 0x82, 0xa2,
         0xaa, 0xea, 0xee, 0xfe
      };
      hierarchy_mask = default_mask[max_levels];
      max_levels = 8; /* the high bit of the mask is always set */
   } else {
      hierarchy_mask = BITFIELD_MASK(max_levels);
   }

   /* Always enable the level covering the whole FB, and disable the finest
    * levels if we don't have enough to cover everything.
    * This is suboptimal for small primitives, since it might force
    * primitives to be walked multiple times even if they don't cover the
    * the tile being processed. On the other hand, it's hard to guess
    * the draw pattern, so it's probably good enough for now.
    */
   if (last_hierarchy_bit > max_levels)
      hierarchy_mask <<= last_hierarchy_bit - max_levels;

   /* Disable hierarchies falling under the effective tile size. */
   uint32_t disable_hierarchies;
   for (disable_hierarchies = 0;
        tile_size > (16 * 16) << (disable_hierarchies * 2);
        disable_hierarchies++)
      ;
   hierarchy_mask &= ~BITFIELD_MASK(disable_hierarchies);

   /* Disable hierachies that would cause the bins to fit in our budget */
   while (disable_hierarchies < PAN_BIN_LEVEL_COUNT) {
      uint32_t bins_ptr_size =
         pan_calc_bins_pointer_size(width, height, tile_size, hierarchy_mask);

      if (bins_ptr_size < mem_budget)
         break;

      disable_hierarchies++;
      hierarchy_mask &= ~BITFIELD_MASK(disable_hierarchies);
   }

   /* We should fit in our budget at this point */
   assert(pan_calc_bins_pointer_size(width, height, tile_size,
                                     hierarchy_mask) <= mem_budget);

   /* Before v12, at least one hierarchy level must be enabled. */
   assert(hierarchy_mask != 0 || PAN_ARCH >= 12);

   return hierarchy_mask;
}
#endif
