/*
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "main/mtypes.h"
#include "main/blit.h"
#include "main/context.h"
#include "main/enums.h"
#include "main/fbobject.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "intel_blit.h"
#include "intel_buffers.h"
#include "intel_fbo.h"
#include "intel_batchbuffer.h"
#include "intel_mipmap_tree.h"

#define FILE_DEBUG_FLAG DEBUG_BLIT

#define SET_TILING_XY_FAST_COPY_BLT(tiling, tr_mode, type)           \
({                                                                   \
   switch (tiling) {                                                 \
   case I915_TILING_X:                                               \
      CMD |= type ## _TILED_X;                                       \
      break;                                                         \
   case I915_TILING_Y:                                               \
      if (tr_mode == INTEL_MIPTREE_TRMODE_YS)                        \
         CMD |= type ## _TILED_64K;                                  \
      else                                                           \
         CMD |= type ## _TILED_Y;                                    \
      break;                                                         \
   default:                                                          \
      unreachable("not reached");                                    \
   }                                                                 \
})

static void
intel_miptree_set_alpha_to_one(struct brw_context *brw,
                               struct intel_mipmap_tree *mt,
                               int x, int y, int width, int height);

static GLuint translate_raster_op(GLenum logicop)
{
   switch(logicop) {
   case GL_CLEAR: return 0x00;
   case GL_AND: return 0x88;
   case GL_AND_REVERSE: return 0x44;
   case GL_COPY: return 0xCC;
   case GL_AND_INVERTED: return 0x22;
   case GL_NOOP: return 0xAA;
   case GL_XOR: return 0x66;
   case GL_OR: return 0xEE;
   case GL_NOR: return 0x11;
   case GL_EQUIV: return 0x99;
   case GL_INVERT: return 0x55;
   case GL_OR_REVERSE: return 0xDD;
   case GL_COPY_INVERTED: return 0x33;
   case GL_OR_INVERTED: return 0xBB;
   case GL_NAND: return 0x77;
   case GL_SET: return 0xFF;
   default: return 0;
   }
}

static uint32_t
br13_for_cpp(int cpp)
{
   switch (cpp) {
   case 16:
      return BR13_32323232;
   case 8:
      return BR13_16161616;
   case 4:
      return BR13_8888;
   case 2:
      return BR13_565;
   case 1:
      return BR13_8;
   default:
      unreachable("not reached");
   }
}

/**
 * Emits the packet for switching the blitter from X to Y tiled or back.
 *
 * This has to be called in a single BEGIN_BATCH_BLT_TILED() /
 * ADVANCE_BATCH_TILED().  This is because BCS_SWCTRL is saved and restored as
 * part of the power context, not a render context, and if the batchbuffer was
 * to get flushed between setting and blitting, or blitting and restoring, our
 * tiling state would leak into other unsuspecting applications (like the X
 * server).
 */
static uint32_t *
set_blitter_tiling(struct brw_context *brw,
                   bool dst_y_tiled, bool src_y_tiled,
                   uint32_t *__map)
{
   assert(brw->gen >= 6);

   /* Idle the blitter before we update how tiling is interpreted. */
   OUT_BATCH(MI_FLUSH_DW);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);

   OUT_BATCH(MI_LOAD_REGISTER_IMM | (3 - 2));
   OUT_BATCH(BCS_SWCTRL);
   OUT_BATCH((BCS_SWCTRL_DST_Y | BCS_SWCTRL_SRC_Y) << 16 |
             (dst_y_tiled ? BCS_SWCTRL_DST_Y : 0) |
             (src_y_tiled ? BCS_SWCTRL_SRC_Y : 0));
   return __map;
}
#define SET_BLITTER_TILING(...) __map = set_blitter_tiling(__VA_ARGS__, __map)

#define BEGIN_BATCH_BLT_TILED(n, dst_y_tiled, src_y_tiled)              \
      BEGIN_BATCH_BLT(n + ((dst_y_tiled || src_y_tiled) ? 14 : 0));     \
      if (dst_y_tiled || src_y_tiled)                                   \
         SET_BLITTER_TILING(brw, dst_y_tiled, src_y_tiled)

#define ADVANCE_BATCH_TILED(dst_y_tiled, src_y_tiled)                   \
      if (dst_y_tiled || src_y_tiled)                                   \
         SET_BLITTER_TILING(brw, false, false);                         \
      ADVANCE_BATCH()

static int
blt_pitch(struct intel_mipmap_tree *mt)
{
   int pitch = mt->pitch;
   if (mt->tiling)
      pitch /= 4;
   return pitch;
}

bool
intel_miptree_blit_compatible_formats(mesa_format src, mesa_format dst)
{
   /* The BLT doesn't handle sRGB conversion */
   assert(src == _mesa_get_srgb_format_linear(src));
   assert(dst == _mesa_get_srgb_format_linear(dst));

   /* No swizzle or format conversions possible, except... */
   if (src == dst)
      return true;

   /* ...we can either discard the alpha channel when going from A->X,
    * or we can fill the alpha channel with 0xff when going from X->A
    */
   if (src == MESA_FORMAT_B8G8R8A8_UNORM || src == MESA_FORMAT_B8G8R8X8_UNORM)
      return (dst == MESA_FORMAT_B8G8R8A8_UNORM ||
              dst == MESA_FORMAT_B8G8R8X8_UNORM);

   if (src == MESA_FORMAT_R8G8B8A8_UNORM || src == MESA_FORMAT_R8G8B8X8_UNORM)
      return (dst == MESA_FORMAT_R8G8B8A8_UNORM ||
              dst == MESA_FORMAT_R8G8B8X8_UNORM);

   return false;
}

static void
get_blit_intratile_offset_el(const struct brw_context *brw,
                             struct intel_mipmap_tree *mt,
                             uint32_t total_x_offset_el,
                             uint32_t total_y_offset_el,
                             uint32_t *base_address_offset,
                             uint32_t *x_offset_el,
                             uint32_t *y_offset_el)
{
   enum isl_tiling tiling = intel_miptree_get_isl_tiling(mt);
   isl_tiling_get_intratile_offset_el(&brw->isl_dev,
                                      tiling, mt->cpp, mt->pitch,
                                      total_x_offset_el, total_y_offset_el,
                                      base_address_offset,
                                      x_offset_el, y_offset_el);
   if (tiling == ISL_TILING_LINEAR) {
      /* From the Broadwell PRM docs for XY_SRC_COPY_BLT::SourceBaseAddress:
       *
       *    "Base address of the destination surface: X=0, Y=0. Lower 32bits
       *    of the 48bit addressing. When Src Tiling is enabled (Bit_15
       *    enabled), this address must be 4KB-aligned. When Tiling is not
       *    enabled, this address should be CL (64byte) aligned."
       *
       * The offsets we get from ISL in the tiled case are already aligned.
       * In the linear case, we need to do some of our own aligning.
       */
      assert(mt->pitch % 64 == 0);
      uint32_t delta = *base_address_offset & 63;
      assert(delta % mt->cpp == 0);
      *base_address_offset -= delta;
      *x_offset_el += delta / mt->cpp;
   } else {
      assert(*base_address_offset % 4096 == 0);
   }
}

static bool
emit_miptree_blit(struct brw_context *brw,
                  struct intel_mipmap_tree *src_mt,
                  uint32_t src_x, uint32_t src_y,
                  struct intel_mipmap_tree *dst_mt,
                  uint32_t dst_x, uint32_t dst_y,
                  uint32_t width, uint32_t height,
                  bool reverse, GLenum logicop)
{
   /* According to the Ivy Bridge PRM, Vol1 Part4, section 1.2.1.2 (Graphics
    * Data Size Limitations):
    *
    *    The BLT engine is capable of transferring very large quantities of
    *    graphics data. Any graphics data read from and written to the
    *    destination is permitted to represent a number of pixels that
    *    occupies up to 65,536 scan lines and up to 32,768 bytes per scan line
    *    at the destination. The maximum number of pixels that may be
    *    represented per scan line’s worth of graphics data depends on the
    *    color depth.
    *
    * The blitter's pitch is a signed 16-bit integer, but measured in bytes
    * for linear surfaces and DWords for tiled surfaces.  So the maximum
    * pitch is 32k linear and 128k tiled.
    */
   if (blt_pitch(src_mt) >= 32768 || blt_pitch(dst_mt) >= 32768) {
      perf_debug("Falling back due to >= 32k/128k pitch\n");
      return false;
   }

   /* We need to split the blit into chunks that each fit within the blitter's
    * restrictions.  We can't use a chunk size of 32768 because we need to
    * ensure that src_tile_x + chunk_size fits.  We choose 16384 because it's
    * a nice round power of two, big enough that performance won't suffer, and
    * small enough to guarantee everything fits.
    */
   const uint32_t max_chunk_size = 16384;

   for (uint32_t chunk_x = 0; chunk_x < width; chunk_x += max_chunk_size) {
      for (uint32_t chunk_y = 0; chunk_y < height; chunk_y += max_chunk_size) {
         const uint32_t chunk_w = MIN2(max_chunk_size, width - chunk_x);
         const uint32_t chunk_h = MIN2(max_chunk_size, height - chunk_y);

         uint32_t src_offset, src_tile_x, src_tile_y;
         get_blit_intratile_offset_el(brw, src_mt,
                                      src_x + chunk_x, src_y + chunk_y,
                                      &src_offset, &src_tile_x, &src_tile_y);

         uint32_t dst_offset, dst_tile_x, dst_tile_y;
         get_blit_intratile_offset_el(brw, dst_mt,
                                      dst_x + chunk_x, dst_y + chunk_y,
                                      &dst_offset, &dst_tile_x, &dst_tile_y);

         if (!intelEmitCopyBlit(brw,
                                src_mt->cpp,
                                reverse ? -src_mt->pitch : src_mt->pitch,
                                src_mt->bo, src_mt->offset + src_offset,
                                src_mt->tiling,
                                src_mt->tr_mode,
                                dst_mt->pitch,
                                dst_mt->bo, dst_mt->offset + dst_offset,
                                dst_mt->tiling,
                                dst_mt->tr_mode,
                                src_tile_x, src_tile_y,
                                dst_tile_x, dst_tile_y,
                                chunk_w, chunk_h,
                                logicop)) {
            /* If this is ever going to fail, it will fail on the first chunk */
            assert(chunk_x == 0 && chunk_y == 0);
            return false;
         }
      }
   }

   return true;
}

/**
 * Implements a rectangular block transfer (blit) of pixels between two
 * miptrees.
 *
 * Our blitter can operate on 1, 2, or 4-byte-per-pixel data, with generous,
 * but limited, pitches and sizes allowed.
 *
 * The src/dst coordinates are relative to the given level/slice of the
 * miptree.
 *
 * If @src_flip or @dst_flip is set, then the rectangle within that miptree
 * will be inverted (including scanline order) when copying.  This is common
 * in GL when copying between window system and user-created
 * renderbuffers/textures.
 */
bool
intel_miptree_blit(struct brw_context *brw,
                   struct intel_mipmap_tree *src_mt,
                   int src_level, int src_slice,
                   uint32_t src_x, uint32_t src_y, bool src_flip,
                   struct intel_mipmap_tree *dst_mt,
                   int dst_level, int dst_slice,
                   uint32_t dst_x, uint32_t dst_y, bool dst_flip,
                   uint32_t width, uint32_t height,
                   GLenum logicop)
{
   /* The blitter doesn't understand multisampling at all. */
   if (src_mt->num_samples > 0 || dst_mt->num_samples > 0)
      return false;

   /* No sRGB decode or encode is done by the hardware blitter, which is
    * consistent with what we want in many callers (glCopyTexSubImage(),
    * texture validation, etc.).
    */
   mesa_format src_format = _mesa_get_srgb_format_linear(src_mt->format);
   mesa_format dst_format = _mesa_get_srgb_format_linear(dst_mt->format);

   /* The blitter doesn't support doing any format conversions.  We do also
    * support blitting ARGB8888 to XRGB8888 (trivial, the values dropped into
    * the X channel don't matter), and XRGB8888 to ARGB8888 by setting the A
    * channel to 1.0 at the end.
    */
   if (!intel_miptree_blit_compatible_formats(src_format, dst_format)) {
      perf_debug("%s: Can't use hardware blitter from %s to %s, "
                 "falling back.\n", __func__,
                 _mesa_get_format_name(src_format),
                 _mesa_get_format_name(dst_format));
      return false;
   }

   /* The blitter has no idea about HiZ or fast color clears, so we need to
    * resolve the miptrees before we do anything.
    */
   intel_miptree_slice_resolve_depth(brw, src_mt, src_level, src_slice);
   intel_miptree_slice_resolve_depth(brw, dst_mt, dst_level, dst_slice);
   intel_miptree_resolve_color(brw, src_mt, src_level, src_slice, 1, 0);
   intel_miptree_resolve_color(brw, dst_mt, dst_level, dst_slice, 1, 0);

   if (src_flip)
      src_y = minify(src_mt->physical_height0, src_level - src_mt->first_level) - src_y - height;

   if (dst_flip)
      dst_y = minify(dst_mt->physical_height0, dst_level - dst_mt->first_level) - dst_y - height;

   uint32_t src_image_x, src_image_y, dst_image_x, dst_image_y;
   intel_miptree_get_image_offset(src_mt, src_level, src_slice,
                                  &src_image_x, &src_image_y);
   intel_miptree_get_image_offset(dst_mt, dst_level, dst_slice,
                                  &dst_image_x, &dst_image_y);
   src_x += src_image_x;
   src_y += src_image_y;
   dst_x += dst_image_x;
   dst_y += dst_image_y;

   if (!emit_miptree_blit(brw, src_mt, src_x, src_y,
                          dst_mt, dst_x, dst_y, width, height,
                          src_flip != dst_flip, logicop)) {
      return false;
   }

   /* XXX This could be done in a single pass using XY_FULL_MONO_PATTERN_BLT */
   if (_mesa_get_format_bits(src_format, GL_ALPHA_BITS) == 0 &&
       _mesa_get_format_bits(dst_format, GL_ALPHA_BITS) > 0) {
      intel_miptree_set_alpha_to_one(brw, dst_mt,
                                     dst_x, dst_y,
                                     width, height);
   }

   return true;
}

bool
intel_miptree_copy(struct brw_context *brw,
                   struct intel_mipmap_tree *src_mt,
                   int src_level, int src_slice,
                   uint32_t src_x, uint32_t src_y,
                   struct intel_mipmap_tree *dst_mt,
                   int dst_level, int dst_slice,
                   uint32_t dst_x, uint32_t dst_y,
                   uint32_t src_width, uint32_t src_height)
{
   /* The blitter doesn't understand multisampling at all. */
   if (src_mt->num_samples > 0 || dst_mt->num_samples > 0)
      return false;

   if (src_mt->format == MESA_FORMAT_S_UINT8)
      return false;

   /* The blitter has no idea about HiZ or fast color clears, so we need to
    * resolve the miptrees before we do anything.
    */
   intel_miptree_slice_resolve_depth(brw, src_mt, src_level, src_slice);
   intel_miptree_slice_resolve_depth(brw, dst_mt, dst_level, dst_slice);
   intel_miptree_resolve_color(brw, src_mt, src_level, src_slice, 1, 0);
   intel_miptree_resolve_color(brw, dst_mt, dst_level, dst_slice, 1, 0);

   uint32_t src_image_x, src_image_y;
   intel_miptree_get_image_offset(src_mt, src_level, src_slice,
                                  &src_image_x, &src_image_y);

   if (_mesa_is_format_compressed(src_mt->format)) {
      GLuint bw, bh;
      _mesa_get_format_block_size(src_mt->format, &bw, &bh);

      /* Compressed textures need not have dimensions that are a multiple of
       * the block size.  Rectangles in compressed textures do need to be a
       * multiple of the block size.  The one exception is that the right and
       * bottom edges may be at the right or bottom edge of the miplevel even
       * if it's not aligned.
       */
      assert(src_x % bw == 0);
      assert(src_y % bh == 0);
      assert(src_width % bw == 0 ||
             src_x + src_width == minify(src_mt->logical_width0, src_level));
      assert(src_height % bh == 0 ||
             src_y + src_height == minify(src_mt->logical_height0, src_level));

      src_x /= (int)bw;
      src_y /= (int)bh;
      src_width /= (int)bw;
      src_height /= (int)bh;
   }
   src_x += src_image_x;
   src_y += src_image_y;

   uint32_t dst_image_x, dst_image_y;
   intel_miptree_get_image_offset(dst_mt, dst_level, dst_slice,
                                  &dst_image_x, &dst_image_y);

   if (_mesa_is_format_compressed(dst_mt->format)) {
      GLuint bw, bh;
      _mesa_get_format_block_size(dst_mt->format, &bw, &bh);

      assert(dst_x % bw == 0);
      assert(dst_y % bh == 0);

      dst_x /= (int)bw;
      dst_y /= (int)bh;
   }
   dst_x += dst_image_x;
   dst_y += dst_image_y;

   return emit_miptree_blit(brw, src_mt, src_x, src_y,
                            dst_mt, dst_x, dst_y,
                            src_width, src_height, false, GL_COPY);
}

static bool
alignment_valid(struct brw_context *brw, unsigned offset, uint32_t tiling)
{
   /* Tiled buffers must be page-aligned (4K). */
   if (tiling != I915_TILING_NONE)
      return (offset & 4095) == 0;

   /* On Gen8+, linear buffers must be cacheline-aligned. */
   if (brw->gen >= 8)
      return (offset & 63) == 0;

   return true;
}

static bool
can_fast_copy_blit(struct brw_context *brw,
		   drm_intel_bo *src_buffer,
                   int16_t src_x, int16_t src_y,
                   uintptr_t src_offset, int32_t src_pitch,
                   uint32_t src_tiling, uint32_t src_tr_mode,
		   drm_intel_bo *dst_buffer,
                   int16_t dst_x, int16_t dst_y,
                   uintptr_t dst_offset, int32_t dst_pitch,
                   uint32_t dst_tiling, uint32_t dst_tr_mode,
                   int16_t w, int16_t h, uint32_t cpp,
                   GLenum logic_op)
{
   const bool dst_tiling_none = dst_tiling == I915_TILING_NONE;
   const bool src_tiling_none = src_tiling == I915_TILING_NONE;

   if (brw->gen < 9)
      return false;

   /* Enable fast copy blit only if the surfaces are Yf/Ys tiled.
    * FIXME: Based on performance data, remove this condition later to
    * enable for all types of surfaces.
    */
   if (src_tr_mode == INTEL_MIPTREE_TRMODE_NONE &&
       dst_tr_mode == INTEL_MIPTREE_TRMODE_NONE)
      return false;

   if (logic_op != GL_COPY)
      return false;

   /* The start pixel for Fast Copy blit should be on an OWord boundary. */
   if ((dst_x * cpp | src_x * cpp) & 15)
      return false;

   /* For all surface types buffers must be cacheline-aligned. */
   if ((dst_offset | src_offset) & 63)
      return false;

   /* Color depths which are not power of 2 or greater than 128 bits are
    * not supported.
    */
   if (!_mesa_is_pow_two(cpp) || cpp > 16)
      return false;

   /* For Fast Copy Blits the pitch cannot be a negative number. */
   if (src_pitch < 0 || dst_pitch < 0)
      return false;

   /* For Linear surfaces, the pitch has to be an OWord (16byte) multiple. */
   if ((src_tiling_none && src_pitch % 16 != 0) ||
       (dst_tiling_none && dst_pitch % 16 != 0))
      return false;

   return true;
}

static uint32_t
xy_blit_cmd(uint32_t src_tiling, uint32_t src_tr_mode,
            uint32_t dst_tiling, uint32_t dst_tr_mode,
            uint32_t cpp, bool use_fast_copy_blit)
{
   uint32_t CMD = 0;

   if (use_fast_copy_blit) {
      CMD = XY_FAST_COPY_BLT_CMD;

      if (dst_tiling != I915_TILING_NONE)
         SET_TILING_XY_FAST_COPY_BLT(dst_tiling, dst_tr_mode, XY_FAST_DST);

      if (src_tiling != I915_TILING_NONE)
         SET_TILING_XY_FAST_COPY_BLT(src_tiling, src_tr_mode, XY_FAST_SRC);
   } else {
      assert(cpp <= 4);
      switch (cpp) {
      case 1:
      case 2:
         CMD = XY_SRC_COPY_BLT_CMD;
         break;
      case 4:
         CMD = XY_SRC_COPY_BLT_CMD | XY_BLT_WRITE_ALPHA | XY_BLT_WRITE_RGB;
         break;
      default:
         unreachable("not reached");
      }

      if (dst_tiling != I915_TILING_NONE)
         CMD |= XY_DST_TILED;

      if (src_tiling != I915_TILING_NONE)
         CMD |= XY_SRC_TILED;
   }
   return CMD;
}

/* Copy BitBlt
 */
bool
intelEmitCopyBlit(struct brw_context *brw,
		  GLuint cpp,
		  int32_t src_pitch,
		  drm_intel_bo *src_buffer,
		  GLuint src_offset,
		  uint32_t src_tiling,
		  uint32_t src_tr_mode,
		  int32_t dst_pitch,
		  drm_intel_bo *dst_buffer,
		  GLuint dst_offset,
		  uint32_t dst_tiling,
		  uint32_t dst_tr_mode,
		  GLshort src_x, GLshort src_y,
		  GLshort dst_x, GLshort dst_y,
		  GLshort w, GLshort h,
		  GLenum logic_op)
{
   GLuint CMD, BR13, pass = 0;
   int dst_y2 = dst_y + h;
   int dst_x2 = dst_x + w;
   drm_intel_bo *aper_array[3];
   bool dst_y_tiled = dst_tiling == I915_TILING_Y;
   bool src_y_tiled = src_tiling == I915_TILING_Y;
   bool use_fast_copy_blit = false;
   uint32_t src_tile_w, src_tile_h;
   uint32_t dst_tile_w, dst_tile_h;

   if ((dst_y_tiled || src_y_tiled) && brw->gen < 6)
      return false;

   /* do space check before going any further */
   do {
       aper_array[0] = brw->batch.bo;
       aper_array[1] = dst_buffer;
       aper_array[2] = src_buffer;

       if (dri_bufmgr_check_aperture_space(aper_array, 3) != 0) {
           intel_batchbuffer_flush(brw);
           pass++;
       } else
           break;
   } while (pass < 2);

   if (pass >= 2)
      return false;

   unsigned length = brw->gen >= 8 ? 10 : 8;

   intel_batchbuffer_require_space(brw, length * 4, BLT_RING);
   DBG("%s src:buf(%p)/%d+%d %d,%d dst:buf(%p)/%d+%d %d,%d sz:%dx%d\n",
       __func__,
       src_buffer, src_pitch, src_offset, src_x, src_y,
       dst_buffer, dst_pitch, dst_offset, dst_x, dst_y, w, h);

   intel_get_tile_dims(src_tiling, src_tr_mode, cpp, &src_tile_w, &src_tile_h);
   intel_get_tile_dims(dst_tiling, dst_tr_mode, cpp, &dst_tile_w, &dst_tile_h);

   /* For Tiled surfaces, the pitch has to be a multiple of the Tile width
    * (X direction width of the Tile). This is ensured while allocating the
    * buffer object.
    */
   assert(src_tiling == I915_TILING_NONE || (src_pitch % src_tile_w) == 0);
   assert(dst_tiling == I915_TILING_NONE || (dst_pitch % dst_tile_w) == 0);

   use_fast_copy_blit = can_fast_copy_blit(brw,
                                           src_buffer,
                                           src_x, src_y,
                                           src_offset, src_pitch,
                                           src_tiling, src_tr_mode,
                                           dst_buffer,
                                           dst_x, dst_y,
                                           dst_offset, dst_pitch,
                                           dst_tiling, dst_tr_mode,
                                           w, h, cpp, logic_op);
   if (!use_fast_copy_blit &&
       (src_tr_mode != INTEL_MIPTREE_TRMODE_NONE ||
        dst_tr_mode != INTEL_MIPTREE_TRMODE_NONE))
      return false;

   if (use_fast_copy_blit) {
      assert(logic_op == GL_COPY);

      /* When two sequential fast copy blits have different source surfaces,
       * but their destinations refer to the same destination surfaces and
       * therefore destinations overlap it is imperative that a flush be
       * inserted between the two blits.
       *
       * FIXME: Figure out a way to avoid flushing when not required.
       */
      brw_emit_mi_flush(brw);

      assert(cpp <= 16);
      BR13 = br13_for_cpp(cpp);

      if (src_tr_mode == INTEL_MIPTREE_TRMODE_YF)
         BR13 |= XY_FAST_SRC_TRMODE_YF;

      if (dst_tr_mode == INTEL_MIPTREE_TRMODE_YF)
         BR13 |= XY_FAST_DST_TRMODE_YF;

      CMD = xy_blit_cmd(src_tiling, src_tr_mode,
                        dst_tiling, dst_tr_mode,
                        cpp, use_fast_copy_blit);

   } else {
      /* For big formats (such as floating point), do the copy using 16 or
       * 32bpp and multiply the coordinates.
       */
      if (cpp > 4) {
         if (cpp % 4 == 2) {
            dst_x *= cpp / 2;
            dst_x2 *= cpp / 2;
            src_x *= cpp / 2;
            cpp = 2;
         } else {
            assert(cpp % 4 == 0);
            dst_x *= cpp / 4;
            dst_x2 *= cpp / 4;
            src_x *= cpp / 4;
            cpp = 4;
         }
      }

      if (!alignment_valid(brw, dst_offset, dst_tiling))
         return false;
      if (!alignment_valid(brw, src_offset, src_tiling))
         return false;

      /* Blit pitch must be dword-aligned.  Otherwise, the hardware appears to drop
       * the low bits.  Offsets must be naturally aligned.
       */
      if (src_pitch % 4 != 0 || src_offset % cpp != 0 ||
          dst_pitch % 4 != 0 || dst_offset % cpp != 0)
         return false;

      assert(cpp <= 4);
      BR13 = br13_for_cpp(cpp) | translate_raster_op(logic_op) << 16;

      CMD = xy_blit_cmd(src_tiling, src_tr_mode,
                        dst_tiling, dst_tr_mode,
                        cpp, use_fast_copy_blit);
   }

   /* For tiled source and destination, pitch value should be specified
    * as a number of Dwords.
    */
   if (dst_tiling != I915_TILING_NONE)
      dst_pitch /= 4;

   if (src_tiling != I915_TILING_NONE)
      src_pitch /= 4;

   if (dst_y2 <= dst_y || dst_x2 <= dst_x)
      return true;

   assert(dst_x < dst_x2);
   assert(dst_y < dst_y2);

   BEGIN_BATCH_BLT_TILED(length, dst_y_tiled, src_y_tiled);
   OUT_BATCH(CMD | (length - 2));
   OUT_BATCH(BR13 | (uint16_t)dst_pitch);
   OUT_BATCH(SET_FIELD(dst_y, BLT_Y) | SET_FIELD(dst_x, BLT_X));
   OUT_BATCH(SET_FIELD(dst_y2, BLT_Y) | SET_FIELD(dst_x2, BLT_X));
   if (brw->gen >= 8) {
      OUT_RELOC64(dst_buffer,
                  I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                  dst_offset);
   } else {
      OUT_RELOC(dst_buffer,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                dst_offset);
   }
   OUT_BATCH(SET_FIELD(src_y, BLT_Y) | SET_FIELD(src_x, BLT_X));
   OUT_BATCH((uint16_t)src_pitch);
   if (brw->gen >= 8) {
      OUT_RELOC64(src_buffer,
                  I915_GEM_DOMAIN_RENDER, 0,
                  src_offset);
   } else {
      OUT_RELOC(src_buffer,
                I915_GEM_DOMAIN_RENDER, 0,
                src_offset);
   }

   ADVANCE_BATCH_TILED(dst_y_tiled, src_y_tiled);

   brw_emit_mi_flush(brw);

   return true;
}

bool
intelEmitImmediateColorExpandBlit(struct brw_context *brw,
				  GLuint cpp,
				  GLubyte *src_bits, GLuint src_size,
				  GLuint fg_color,
				  GLshort dst_pitch,
				  drm_intel_bo *dst_buffer,
				  GLuint dst_offset,
				  uint32_t dst_tiling,
				  GLshort x, GLshort y,
				  GLshort w, GLshort h,
				  GLenum logic_op)
{
   int dwords = ALIGN(src_size, 8) / 4;
   uint32_t opcode, br13, blit_cmd;

   if (dst_tiling != I915_TILING_NONE) {
      if (dst_offset & 4095)
	 return false;
      if (dst_tiling == I915_TILING_Y)
	 return false;
   }

   assert((logic_op >= GL_CLEAR) && (logic_op <= (GL_CLEAR + 0x0f)));
   assert(dst_pitch > 0);

   if (w < 0 || h < 0)
      return true;

   DBG("%s dst:buf(%p)/%d+%d %d,%d sz:%dx%d, %d bytes %d dwords\n",
       __func__,
       dst_buffer, dst_pitch, dst_offset, x, y, w, h, src_size, dwords);

   unsigned xy_setup_blt_length = brw->gen >= 8 ? 10 : 8;
   intel_batchbuffer_require_space(brw, (xy_setup_blt_length * 4) +
                                        (3 * 4) + dwords * 4, BLT_RING);

   opcode = XY_SETUP_BLT_CMD;
   if (cpp == 4)
      opcode |= XY_BLT_WRITE_ALPHA | XY_BLT_WRITE_RGB;
   if (dst_tiling != I915_TILING_NONE) {
      opcode |= XY_DST_TILED;
      dst_pitch /= 4;
   }

   br13 = dst_pitch | (translate_raster_op(logic_op) << 16) | (1 << 29);
   br13 |= br13_for_cpp(cpp);

   blit_cmd = XY_TEXT_IMMEDIATE_BLIT_CMD | XY_TEXT_BYTE_PACKED; /* packing? */
   if (dst_tiling != I915_TILING_NONE)
      blit_cmd |= XY_DST_TILED;

   BEGIN_BATCH_BLT(xy_setup_blt_length + 3);
   OUT_BATCH(opcode | (xy_setup_blt_length - 2));
   OUT_BATCH(br13);
   OUT_BATCH((0 << 16) | 0); /* clip x1, y1 */
   OUT_BATCH((100 << 16) | 100); /* clip x2, y2 */
   if (brw->gen >= 8) {
      OUT_RELOC64(dst_buffer,
                  I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                  dst_offset);
   } else {
      OUT_RELOC(dst_buffer,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                dst_offset);
   }
   OUT_BATCH(0); /* bg */
   OUT_BATCH(fg_color); /* fg */
   OUT_BATCH(0); /* pattern base addr */
   if (brw->gen >= 8)
      OUT_BATCH(0);

   OUT_BATCH(blit_cmd | ((3 - 2) + dwords));
   OUT_BATCH(SET_FIELD(y, BLT_Y) | SET_FIELD(x, BLT_X));
   OUT_BATCH(SET_FIELD(y + h, BLT_Y) | SET_FIELD(x + w, BLT_X));
   ADVANCE_BATCH();

   intel_batchbuffer_data(brw, src_bits, dwords * 4, BLT_RING);

   brw_emit_mi_flush(brw);

   return true;
}

/* We don't have a memmove-type blit like some other hardware, so we'll do a
 * rectangular blit covering a large space, then emit 1-scanline blit at the
 * end to cover the last if we need.
 */
void
intel_emit_linear_blit(struct brw_context *brw,
		       drm_intel_bo *dst_bo,
		       unsigned int dst_offset,
		       drm_intel_bo *src_bo,
		       unsigned int src_offset,
		       unsigned int size)
{
   struct gl_context *ctx = &brw->ctx;
   GLuint pitch, height;
   int16_t src_x, dst_x;
   bool ok;

   do {
      /* The pitch given to the GPU must be DWORD aligned, and
       * we want width to match pitch. Max width is (1 << 15 - 1),
       * rounding that down to the nearest DWORD is 1 << 15 - 4
       */
      pitch = ROUND_DOWN_TO(MIN2(size, (1 << 15) - 64), 4);
      height = (size < pitch || pitch == 0) ? 1 : size / pitch;

      src_x = src_offset % 64;
      dst_x = dst_offset % 64;
      pitch = ALIGN(MIN2(size, (1 << 15) - 64), 4);
      assert(src_x + pitch < 1 << 15);
      assert(dst_x + pitch < 1 << 15);

      ok = intelEmitCopyBlit(brw, 1,
                             pitch, src_bo, src_offset - src_x, I915_TILING_NONE,
                             INTEL_MIPTREE_TRMODE_NONE,
                             pitch, dst_bo, dst_offset - dst_x, I915_TILING_NONE,
                             INTEL_MIPTREE_TRMODE_NONE,
                             src_x, 0, /* src x/y */
                             dst_x, 0, /* dst x/y */
                             MIN2(size, pitch), height, /* w, h */
                             GL_COPY);
      if (!ok) {
         _mesa_problem(ctx, "Failed to linear blit %dx%d\n",
                       MIN2(size, pitch), height);
         return;
      }

      pitch *= height;
      if (size <= pitch)
         return;

      src_offset += pitch;
      dst_offset += pitch;
      size -= pitch;
   } while (1);
}

/**
 * Used to initialize the alpha value of an ARGB8888 miptree after copying
 * into it from an XRGB8888 source.
 *
 * This is very common with glCopyTexImage2D().  Note that the coordinates are
 * relative to the start of the miptree, not relative to a slice within the
 * miptree.
 */
static void
intel_miptree_set_alpha_to_one(struct brw_context *brw,
                              struct intel_mipmap_tree *mt,
                              int x, int y, int width, int height)
{
   uint32_t BR13, CMD;
   int pitch, cpp;
   drm_intel_bo *aper_array[2];

   pitch = mt->pitch;
   cpp = mt->cpp;

   DBG("%s dst:buf(%p)/%d %d,%d sz:%dx%d\n",
       __func__, mt->bo, pitch, x, y, width, height);

   BR13 = br13_for_cpp(cpp) | 0xf0 << 16;
   CMD = XY_COLOR_BLT_CMD;
   CMD |= XY_BLT_WRITE_ALPHA;

   if (mt->tiling != I915_TILING_NONE) {
      CMD |= XY_DST_TILED;
      pitch /= 4;
   }
   BR13 |= pitch;

   /* do space check before going any further */
   aper_array[0] = brw->batch.bo;
   aper_array[1] = mt->bo;

   if (drm_intel_bufmgr_check_aperture_space(aper_array,
					     ARRAY_SIZE(aper_array)) != 0) {
      intel_batchbuffer_flush(brw);
   }

   unsigned length = brw->gen >= 8 ? 7 : 6;
   bool dst_y_tiled = mt->tiling == I915_TILING_Y;

   /* We need to split the blit into chunks that each fit within the blitter's
    * restrictions.  We can't use a chunk size of 32768 because we need to
    * ensure that src_tile_x + chunk_size fits.  We choose 16384 because it's
    * a nice round power of two, big enough that performance won't suffer, and
    * small enough to guarantee everything fits.
    */
   const uint32_t max_chunk_size = 16384;

   for (uint32_t chunk_x = 0; chunk_x < width; chunk_x += max_chunk_size) {
      for (uint32_t chunk_y = 0; chunk_y < height; chunk_y += max_chunk_size) {
         const uint32_t chunk_w = MIN2(max_chunk_size, width - chunk_x);
         const uint32_t chunk_h = MIN2(max_chunk_size, height - chunk_y);

         uint32_t offset, tile_x, tile_y;
         get_blit_intratile_offset_el(brw, mt,
                                      x + chunk_x, y + chunk_y,
                                      &offset, &tile_x, &tile_y);

         BEGIN_BATCH_BLT_TILED(length, dst_y_tiled, false);
         OUT_BATCH(CMD | (length - 2));
         OUT_BATCH(BR13);
         OUT_BATCH(SET_FIELD(y + chunk_y, BLT_Y) |
                   SET_FIELD(x + chunk_x, BLT_X));
         OUT_BATCH(SET_FIELD(y + chunk_y + chunk_h, BLT_Y) |
                   SET_FIELD(x + chunk_x + chunk_w, BLT_X));
         if (brw->gen >= 8) {
            OUT_RELOC64(mt->bo,
                        I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                        offset);
         } else {
            OUT_RELOC(mt->bo,
                      I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                      offset);
         }
         OUT_BATCH(0xffffffff); /* white, but only alpha gets written */
         ADVANCE_BATCH_TILED(dst_y_tiled, false);
      }
   }

   brw_emit_mi_flush(brw);
}
