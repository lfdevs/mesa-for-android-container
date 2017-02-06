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

#include "main/context.h"
#include "main/teximage.h"
#include "main/blend.h"
#include "main/fbobject.h"
#include "main/renderbuffer.h"
#include "main/glformats.h"

#include "brw_blorp.h"
#include "brw_context.h"
#include "brw_defines.h"
#include "brw_meta_util.h"
#include "brw_state.h"
#include "intel_fbo.h"
#include "intel_debug.h"

#define FILE_DEBUG_FLAG DEBUG_BLORP

static bool
brw_blorp_lookup_shader(struct blorp_context *blorp,
                        const void *key, uint32_t key_size,
                        uint32_t *kernel_out, void *prog_data_out)
{
   struct brw_context *brw = blorp->driver_ctx;
   return brw_search_cache(&brw->cache, BRW_CACHE_BLORP_PROG,
                           key, key_size, kernel_out, prog_data_out);
}

static void
brw_blorp_upload_shader(struct blorp_context *blorp,
                        const void *key, uint32_t key_size,
                        const void *kernel, uint32_t kernel_size,
                        const struct brw_stage_prog_data *prog_data,
                        uint32_t prog_data_size,
                        uint32_t *kernel_out, void *prog_data_out)
{
   struct brw_context *brw = blorp->driver_ctx;
   brw_upload_cache(&brw->cache, BRW_CACHE_BLORP_PROG, key, key_size,
                    kernel, kernel_size, prog_data, prog_data_size,
                    kernel_out, prog_data_out);
}

void
brw_blorp_init(struct brw_context *brw)
{
   blorp_init(&brw->blorp, brw, &brw->isl_dev);

   brw->blorp.compiler = brw->screen->compiler;

   switch (brw->gen) {
   case 6:
      brw->blorp.mocs.tex = 0;
      brw->blorp.mocs.rb = 0;
      brw->blorp.mocs.vb = 0;
      brw->blorp.exec = gen6_blorp_exec;
      break;
   case 7:
      brw->blorp.mocs.tex = GEN7_MOCS_L3;
      brw->blorp.mocs.rb = GEN7_MOCS_L3;
      brw->blorp.mocs.vb = GEN7_MOCS_L3;
      if (brw->is_haswell) {
         brw->blorp.exec = gen75_blorp_exec;
      } else {
         brw->blorp.exec = gen7_blorp_exec;
      }
      break;
   case 8:
      brw->blorp.mocs.tex = BDW_MOCS_WB;
      brw->blorp.mocs.rb = BDW_MOCS_PTE;
      brw->blorp.mocs.vb = BDW_MOCS_WB;
      brw->blorp.exec = gen8_blorp_exec;
      break;
   case 9:
      brw->blorp.mocs.tex = SKL_MOCS_WB;
      brw->blorp.mocs.rb = SKL_MOCS_PTE;
      brw->blorp.mocs.vb = SKL_MOCS_WB;
      brw->blorp.exec = gen9_blorp_exec;
      break;
   default:
      unreachable("Invalid gen");
   }

   brw->blorp.lookup_shader = brw_blorp_lookup_shader;
   brw->blorp.upload_shader = brw_blorp_upload_shader;
}

static void
apply_gen6_stencil_hiz_offset(struct isl_surf *surf,
                              struct intel_mipmap_tree *mt,
                              uint32_t lod,
                              uint32_t *offset)
{
   assert(mt->array_layout == ALL_SLICES_AT_EACH_LOD);

   if (mt->format == MESA_FORMAT_S_UINT8) {
      /* Note: we can't compute the stencil offset using
       * intel_miptree_get_aligned_offset(), because the miptree
       * claims that the region is untiled even though it's W tiled.
       */
      *offset = mt->level[lod].level_y * mt->pitch +
                mt->level[lod].level_x * 64;
   } else {
      *offset = intel_miptree_get_aligned_offset(mt,
                                                 mt->level[lod].level_x,
                                                 mt->level[lod].level_y);
   }

   surf->logical_level0_px.width = minify(surf->logical_level0_px.width, lod);
   surf->logical_level0_px.height = minify(surf->logical_level0_px.height, lod);
   surf->phys_level0_sa.width = minify(surf->phys_level0_sa.width, lod);
   surf->phys_level0_sa.height = minify(surf->phys_level0_sa.height, lod);
   surf->levels = 1;
   surf->array_pitch_el_rows =
      ALIGN(surf->phys_level0_sa.height, surf->image_alignment_el.height);
}

static void
blorp_surf_for_miptree(struct brw_context *brw,
                       struct blorp_surf *surf,
                       struct intel_mipmap_tree *mt,
                       bool is_render_target,
                       uint32_t safe_aux_usage,
                       unsigned *level,
                       unsigned start_layer, unsigned num_layers,
                       struct isl_surf tmp_surfs[2])
{
   if (mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
       mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS) {
      const unsigned num_samples = MAX2(1, mt->num_samples);
      for (unsigned i = 0; i < num_layers; i++) {
         for (unsigned s = 0; s < num_samples; s++) {
            const unsigned phys_layer = (start_layer + i) * num_samples + s;
            intel_miptree_check_level_layer(mt, *level, phys_layer);
         }
      }
   } else {
      for (unsigned i = 0; i < num_layers; i++)
         intel_miptree_check_level_layer(mt, *level, start_layer + i);
   }

   intel_miptree_get_isl_surf(brw, mt, &tmp_surfs[0]);
   surf->surf = &tmp_surfs[0];
   surf->addr = (struct blorp_address) {
      .buffer = mt->bo,
      .offset = mt->offset,
      .read_domains = is_render_target ? I915_GEM_DOMAIN_RENDER :
                                         I915_GEM_DOMAIN_SAMPLER,
      .write_domain = is_render_target ? I915_GEM_DOMAIN_RENDER : 0,
   };

   if (brw->gen == 6 && mt->format == MESA_FORMAT_S_UINT8 &&
       mt->array_layout == ALL_SLICES_AT_EACH_LOD) {
      /* Sandy bridge stencil and HiZ use this ALL_SLICES_AT_EACH_LOD hack in
       * order to allow for layered rendering.  The hack makes each LOD of the
       * stencil or HiZ buffer a single tightly packed array surface at some
       * offset into the surface.  Since ISL doesn't know how to deal with the
       * crazy ALL_SLICES_AT_EACH_LOD layout and since we have to do a manual
       * offset of it anyway, we might as well do the offset here and keep the
       * hacks inside the i965 driver.
       *
       * See also gen6_depth_stencil_state.c
       */
      uint32_t offset;
      apply_gen6_stencil_hiz_offset(&tmp_surfs[0], mt, *level, &offset);
      surf->addr.offset += offset;
      *level = 0;
   }

   struct isl_surf *aux_surf = &tmp_surfs[1];
   intel_miptree_get_aux_isl_surf(brw, mt, aux_surf, &surf->aux_usage);

   if (surf->aux_usage != ISL_AUX_USAGE_NONE) {
      if (surf->aux_usage == ISL_AUX_USAGE_HIZ) {
         /* If we're not going to use it as a depth buffer, resolve HiZ */
         if (!(safe_aux_usage & (1 << ISL_AUX_USAGE_HIZ))) {
            for (unsigned i = 0; i < num_layers; i++) {
               intel_miptree_slice_resolve_depth(brw, mt, *level,
                                                 start_layer + i);

               /* If we're rendering to it then we'll need a HiZ resolve once
                * we're done before we can use it with HiZ again.
                */
               if (is_render_target)
                  intel_miptree_slice_set_needs_hiz_resolve(mt, *level,
                                                            start_layer + i);
            }
            surf->aux_usage = ISL_AUX_USAGE_NONE;
         }
      } else if (!(safe_aux_usage & (1 << surf->aux_usage))) {
         uint32_t flags = 0;
         if (safe_aux_usage & (1 << ISL_AUX_USAGE_CCS_E))
            flags |= INTEL_MIPTREE_IGNORE_CCS_E;

         intel_miptree_resolve_color(brw, mt,
                                     *level, start_layer, num_layers, flags);

         assert(!intel_miptree_has_color_unresolved(mt, *level, 1,
                                                    start_layer, num_layers));
         surf->aux_usage = ISL_AUX_USAGE_NONE;
      }
   }

   if (is_render_target)
      intel_miptree_used_for_rendering(brw, mt, *level,
                                       start_layer, num_layers);

   if (surf->aux_usage != ISL_AUX_USAGE_NONE) {
      /* We only really need a clear color if we also have an auxiliary
       * surface.  Without one, it does nothing.
       */
      surf->clear_color = intel_miptree_get_isl_clear_color(brw, mt);

      surf->aux_surf = aux_surf;
      surf->aux_addr = (struct blorp_address) {
         .read_domains = is_render_target ? I915_GEM_DOMAIN_RENDER :
                                            I915_GEM_DOMAIN_SAMPLER,
         .write_domain = is_render_target ? I915_GEM_DOMAIN_RENDER : 0,
      };

      if (mt->mcs_buf) {
         surf->aux_addr.buffer = mt->mcs_buf->bo;
         surf->aux_addr.offset = mt->mcs_buf->offset;
      } else {
         assert(surf->aux_usage == ISL_AUX_USAGE_HIZ);
         struct intel_mipmap_tree *hiz_mt = mt->hiz_buf->mt;
         if (hiz_mt) {
            surf->aux_addr.buffer = hiz_mt->bo;
            if (brw->gen == 6 &&
                hiz_mt->array_layout == ALL_SLICES_AT_EACH_LOD) {
               /* gen6 requires the HiZ buffer to be manually offset to the
                * right location.  We could fixup the surf but it doesn't
                * matter since most of those fields don't matter.
                */
               apply_gen6_stencil_hiz_offset(aux_surf, hiz_mt, *level,
                                             &surf->aux_addr.offset);
            } else {
               surf->aux_addr.offset = 0;
            }
            assert(hiz_mt->pitch == aux_surf->row_pitch);
         } else {
            surf->aux_addr.buffer = mt->hiz_buf->aux_base.bo;
            surf->aux_addr.offset = mt->hiz_buf->aux_base.offset;
         }
      }
   } else {
      surf->aux_addr = (struct blorp_address) {
         .buffer = NULL,
      };
      memset(&surf->clear_color, 0, sizeof(surf->clear_color));
   }
   assert((surf->aux_usage == ISL_AUX_USAGE_NONE) ==
          (surf->aux_addr.buffer == NULL));
}

static enum isl_format
brw_blorp_to_isl_format(struct brw_context *brw, mesa_format format,
                        bool is_render_target)
{
   switch (format) {
   case MESA_FORMAT_NONE:
      return ISL_FORMAT_UNSUPPORTED;
   case MESA_FORMAT_S_UINT8:
      return ISL_FORMAT_R8_UINT;
   case MESA_FORMAT_Z24_UNORM_X8_UINT:
   case MESA_FORMAT_Z24_UNORM_S8_UINT:
      return ISL_FORMAT_R24_UNORM_X8_TYPELESS;
   case MESA_FORMAT_Z_FLOAT32:
   case MESA_FORMAT_Z32_FLOAT_S8X24_UINT:
      return ISL_FORMAT_R32_FLOAT;
   case MESA_FORMAT_Z_UNORM16:
      return ISL_FORMAT_R16_UNORM;
   default: {
      if (is_render_target) {
         assert(brw->format_supported_as_render_target[format]);
         return brw->render_target_format[format];
      } else {
         return brw_format_for_mesa_format(format);
      }
      break;
   }
   }
}

/**
 * Convert an swizzle enumeration (i.e. SWIZZLE_X) to one of the Gen7.5+
 * "Shader Channel Select" enumerations (i.e. HSW_SCS_RED).  The mappings are
 *
 * SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W, SWIZZLE_ZERO, SWIZZLE_ONE
 *         0          1          2          3             4            5
 *         4          5          6          7             0            1
 *   SCS_RED, SCS_GREEN,  SCS_BLUE, SCS_ALPHA,     SCS_ZERO,     SCS_ONE
 *
 * which is simply adding 4 then modding by 8 (or anding with 7).
 *
 * We then may need to apply workarounds for textureGather hardware bugs.
 */
static enum isl_channel_select
swizzle_to_scs(GLenum swizzle)
{
   return (enum isl_channel_select)((swizzle + 4) & 7);
}

static unsigned
physical_to_logical_layer(struct intel_mipmap_tree *mt,
                          unsigned physical_layer)
{
   if (mt->num_samples > 1 &&
       (mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
        mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS)) {
      assert(physical_layer % mt->num_samples == 0);
      return physical_layer / mt->num_samples;
   } else {
      return physical_layer;
   }
}

/**
 * Note: if the src (or dst) is a 2D multisample array texture on Gen7+ using
 * INTEL_MSAA_LAYOUT_UMS or INTEL_MSAA_LAYOUT_CMS, src_layer (dst_layer) is
 * the physical layer holding sample 0.  So, for example, if
 * src_mt->num_samples == 4, then logical layer n corresponds to src_layer ==
 * 4*n.
 */
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
                        bool decode_srgb, bool encode_srgb)
{
   /* Blorp operates in logical layers */
   src_layer = physical_to_logical_layer(src_mt, src_layer);
   dst_layer = physical_to_logical_layer(dst_mt, dst_layer);

   DBG("%s from %dx %s mt %p %d %d (%f,%f) (%f,%f)"
       "to %dx %s mt %p %d %d (%f,%f) (%f,%f) (flip %d,%d)\n",
       __func__,
       src_mt->num_samples, _mesa_get_format_name(src_mt->format), src_mt,
       src_level, src_layer, src_x0, src_y0, src_x1, src_y1,
       dst_mt->num_samples, _mesa_get_format_name(dst_mt->format), dst_mt,
       dst_level, dst_layer, dst_x0, dst_y0, dst_x1, dst_y1,
       mirror_x, mirror_y);

   if (!decode_srgb && _mesa_get_format_color_encoding(src_format) == GL_SRGB)
      src_format = _mesa_get_srgb_format_linear(src_format);

   if (!encode_srgb && _mesa_get_format_color_encoding(dst_format) == GL_SRGB)
      dst_format = _mesa_get_srgb_format_linear(dst_format);

   /* When doing a multisample resolve of a GL_LUMINANCE32F or GL_INTENSITY32F
    * texture, the above code configures the source format for L32_FLOAT or
    * I32_FLOAT, and the destination format for R32_FLOAT.  On Sandy Bridge,
    * the SAMPLE message appears to handle multisampled L32_FLOAT and
    * I32_FLOAT textures incorrectly, resulting in blocky artifacts.  So work
    * around the problem by using a source format of R32_FLOAT.  This
    * shouldn't affect rendering correctness, since the destination format is
    * R32_FLOAT, so only the contents of the red channel matters.
    */
   if (brw->gen == 6 &&
       src_mt->num_samples > 1 && dst_mt->num_samples <= 1 &&
       src_mt->format == dst_mt->format &&
       (dst_format == MESA_FORMAT_L_FLOAT32 ||
        dst_format == MESA_FORMAT_I_FLOAT32)) {
      src_format = dst_format = MESA_FORMAT_R_FLOAT32;
   }

   uint32_t src_usage_flags = (1 << ISL_AUX_USAGE_MCS);
   if (src_format == src_mt->format)
      src_usage_flags |= (1 << ISL_AUX_USAGE_CCS_E);

   uint32_t dst_usage_flags = (1 << ISL_AUX_USAGE_MCS);
   if (dst_format == dst_mt->format) {
      dst_usage_flags |= (1 << ISL_AUX_USAGE_CCS_E) |
                         (1 << ISL_AUX_USAGE_CCS_D);
   }

   struct isl_surf tmp_surfs[4];
   struct blorp_surf src_surf, dst_surf;
   blorp_surf_for_miptree(brw, &src_surf, src_mt, false, src_usage_flags,
                          &src_level, src_layer, 1, &tmp_surfs[0]);
   blorp_surf_for_miptree(brw, &dst_surf, dst_mt, true, dst_usage_flags,
                          &dst_level, dst_layer, 1, &tmp_surfs[2]);

   struct isl_swizzle src_isl_swizzle = {
      .r = swizzle_to_scs(GET_SWZ(src_swizzle, 0)),
      .g = swizzle_to_scs(GET_SWZ(src_swizzle, 1)),
      .b = swizzle_to_scs(GET_SWZ(src_swizzle, 2)),
      .a = swizzle_to_scs(GET_SWZ(src_swizzle, 3)),
   };

   struct blorp_batch batch;
   blorp_batch_init(&brw->blorp, &batch, brw, 0);
   blorp_blit(&batch, &src_surf, src_level, src_layer,
              brw_blorp_to_isl_format(brw, src_format, false), src_isl_swizzle,
              &dst_surf, dst_level, dst_layer,
              brw_blorp_to_isl_format(brw, dst_format, true),
              ISL_SWIZZLE_IDENTITY,
              src_x0, src_y0, src_x1, src_y1,
              dst_x0, dst_y0, dst_x1, dst_y1,
              filter, mirror_x, mirror_y);
   blorp_batch_finish(&batch);
}

void
brw_blorp_copy_miptrees(struct brw_context *brw,
                        struct intel_mipmap_tree *src_mt,
                        unsigned src_level, unsigned src_layer,
                        struct intel_mipmap_tree *dst_mt,
                        unsigned dst_level, unsigned dst_layer,
                        unsigned src_x, unsigned src_y,
                        unsigned dst_x, unsigned dst_y,
                        unsigned src_width, unsigned src_height)
{
   DBG("%s from %dx %s mt %p %d %d (%d,%d) %dx%d"
       "to %dx %s mt %p %d %d (%d,%d)\n",
       __func__,
       src_mt->num_samples, _mesa_get_format_name(src_mt->format), src_mt,
       src_level, src_layer, src_x, src_y, src_width, src_height,
       dst_mt->num_samples, _mesa_get_format_name(dst_mt->format), dst_mt,
       dst_level, dst_layer, dst_x, dst_y);

   struct isl_surf tmp_surfs[4];
   struct blorp_surf src_surf, dst_surf;
   blorp_surf_for_miptree(brw, &src_surf, src_mt, false,
                          (1 << ISL_AUX_USAGE_MCS) |
                          (1 << ISL_AUX_USAGE_CCS_E),
                          &src_level, src_layer, 1, &tmp_surfs[0]);
   blorp_surf_for_miptree(brw, &dst_surf, dst_mt, true,
                          (1 << ISL_AUX_USAGE_MCS) |
                          (1 << ISL_AUX_USAGE_CCS_E),
                          &dst_level, dst_layer, 1, &tmp_surfs[2]);

   struct blorp_batch batch;
   blorp_batch_init(&brw->blorp, &batch, brw, 0);
   blorp_copy(&batch, &src_surf, src_level, src_layer,
              &dst_surf, dst_level, dst_layer,
              src_x, src_y, dst_x, dst_y, src_width, src_height);
   blorp_batch_finish(&batch);
}

static struct intel_mipmap_tree *
find_miptree(GLbitfield buffer_bit, struct intel_renderbuffer *irb)
{
   struct intel_mipmap_tree *mt = irb->mt;
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && mt->stencil_mt)
      mt = mt->stencil_mt;
   return mt;
}

static int
blorp_get_texture_swizzle(const struct intel_renderbuffer *irb)
{
   return irb->Base.Base._BaseFormat == GL_RGB ?
      MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_ONE) :
      SWIZZLE_XYZW;
}

static void
do_blorp_blit(struct brw_context *brw, GLbitfield buffer_bit,
              struct intel_renderbuffer *src_irb, mesa_format src_format,
              struct intel_renderbuffer *dst_irb, mesa_format dst_format,
              GLfloat srcX0, GLfloat srcY0, GLfloat srcX1, GLfloat srcY1,
              GLfloat dstX0, GLfloat dstY0, GLfloat dstX1, GLfloat dstY1,
              GLenum filter, bool mirror_x, bool mirror_y)
{
   const struct gl_context *ctx = &brw->ctx;

   /* Find source/dst miptrees */
   struct intel_mipmap_tree *src_mt = find_miptree(buffer_bit, src_irb);
   struct intel_mipmap_tree *dst_mt = find_miptree(buffer_bit, dst_irb);

   const bool do_srgb = ctx->Color.sRGBEnabled;

   /* Do the blit */
   brw_blorp_blit_miptrees(brw,
                           src_mt, src_irb->mt_level, src_irb->mt_layer,
                           src_format, blorp_get_texture_swizzle(src_irb),
                           dst_mt, dst_irb->mt_level, dst_irb->mt_layer,
                           dst_format,
                           srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1,
                           filter, mirror_x, mirror_y,
                           do_srgb, do_srgb);

   dst_irb->need_downsample = true;
}

static bool
try_blorp_blit(struct brw_context *brw,
               const struct gl_framebuffer *read_fb,
               const struct gl_framebuffer *draw_fb,
               GLfloat srcX0, GLfloat srcY0, GLfloat srcX1, GLfloat srcY1,
               GLfloat dstX0, GLfloat dstY0, GLfloat dstX1, GLfloat dstY1,
               GLenum filter, GLbitfield buffer_bit)
{
   struct gl_context *ctx = &brw->ctx;

   /* Sync up the state of window system buffers.  We need to do this before
    * we go looking for the buffers.
    */
   intel_prepare_render(brw);

   bool mirror_x, mirror_y;
   if (brw_meta_mirror_clip_and_scissor(ctx, read_fb, draw_fb,
                                        &srcX0, &srcY0, &srcX1, &srcY1,
                                        &dstX0, &dstY0, &dstX1, &dstY1,
                                        &mirror_x, &mirror_y))
      return true;

   /* Find buffers */
   struct intel_renderbuffer *src_irb;
   struct intel_renderbuffer *dst_irb;
   struct intel_mipmap_tree *src_mt;
   struct intel_mipmap_tree *dst_mt;
   switch (buffer_bit) {
   case GL_COLOR_BUFFER_BIT:
      src_irb = intel_renderbuffer(read_fb->_ColorReadBuffer);
      for (unsigned i = 0; i < draw_fb->_NumColorDrawBuffers; ++i) {
         dst_irb = intel_renderbuffer(draw_fb->_ColorDrawBuffers[i]);
	 if (dst_irb)
            do_blorp_blit(brw, buffer_bit,
                          src_irb, src_irb->Base.Base.Format,
                          dst_irb, dst_irb->Base.Base.Format,
                          srcX0, srcY0, srcX1, srcY1,
                          dstX0, dstY0, dstX1, dstY1,
                          filter, mirror_x, mirror_y);
      }
      break;
   case GL_DEPTH_BUFFER_BIT:
      src_irb =
         intel_renderbuffer(read_fb->Attachment[BUFFER_DEPTH].Renderbuffer);
      dst_irb =
         intel_renderbuffer(draw_fb->Attachment[BUFFER_DEPTH].Renderbuffer);
      src_mt = find_miptree(buffer_bit, src_irb);
      dst_mt = find_miptree(buffer_bit, dst_irb);

      /* We can't handle format conversions between Z24 and other formats
       * since we have to lie about the surface format. See the comments in
       * brw_blorp_surface_info::set().
       */
      if ((src_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT) !=
          (dst_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT))
         return false;

      do_blorp_blit(brw, buffer_bit, src_irb, MESA_FORMAT_NONE,
                    dst_irb, MESA_FORMAT_NONE, srcX0, srcY0,
                    srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                    filter, mirror_x, mirror_y);
      break;
   case GL_STENCIL_BUFFER_BIT:
      src_irb =
         intel_renderbuffer(read_fb->Attachment[BUFFER_STENCIL].Renderbuffer);
      dst_irb =
         intel_renderbuffer(draw_fb->Attachment[BUFFER_STENCIL].Renderbuffer);
      do_blorp_blit(brw, buffer_bit, src_irb, MESA_FORMAT_NONE,
                    dst_irb, MESA_FORMAT_NONE, srcX0, srcY0,
                    srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                    filter, mirror_x, mirror_y);
      break;
   default:
      unreachable("not reached");
   }

   return true;
}

bool
brw_blorp_copytexsubimage(struct brw_context *brw,
                          struct gl_renderbuffer *src_rb,
                          struct gl_texture_image *dst_image,
                          int slice,
                          int srcX0, int srcY0,
                          int dstX0, int dstY0,
                          int width, int height)
{
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *src_irb = intel_renderbuffer(src_rb);
   struct intel_texture_image *intel_image = intel_texture_image(dst_image);

   /* No pixel transfer operations (zoom, bias, mapping), just a blit */
   if (brw->ctx._ImageTransferState)
      return false;

   /* Sync up the state of window system buffers.  We need to do this before
    * we go looking at the src renderbuffer's miptree.
    */
   intel_prepare_render(brw);

   struct intel_mipmap_tree *src_mt = src_irb->mt;
   struct intel_mipmap_tree *dst_mt = intel_image->mt;

   /* There is support for only up to eight samples. */
   if (src_mt->num_samples > 8 || dst_mt->num_samples > 8)
      return false;

   /* BLORP is only supported from Gen6 onwards. */
   if (brw->gen < 6)
      return false;

   if (_mesa_get_format_base_format(src_rb->Format) !=
       _mesa_get_format_base_format(dst_image->TexFormat)) {
      return false;
   }

   /* We can't handle format conversions between Z24 and other formats since
    * we have to lie about the surface format.  See the comments in
    * brw_blorp_surface_info::set().
    */
   if ((src_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT) !=
       (dst_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT)) {
      return false;
   }

   if (!brw->format_supported_as_render_target[dst_image->TexFormat])
      return false;

   /* Source clipping shouldn't be necessary, since copytexsubimage (in
    * src/mesa/main/teximage.c) calls _mesa_clip_copytexsubimage() which
    * takes care of it.
    *
    * Destination clipping shouldn't be necessary since the restrictions on
    * glCopyTexSubImage prevent the user from specifying a destination rectangle
    * that falls outside the bounds of the destination texture.
    * See error_check_subtexture_dimensions().
    */

   int srcY1 = srcY0 + height;
   int srcX1 = srcX0 + width;
   int dstX1 = dstX0 + width;
   int dstY1 = dstY0 + height;

   /* Account for the fact that in the system framebuffer, the origin is at
    * the lower left.
    */
   bool mirror_y = false;
   if (_mesa_is_winsys_fbo(ctx->ReadBuffer)) {
      GLint tmp = src_rb->Height - srcY0;
      srcY0 = src_rb->Height - srcY1;
      srcY1 = tmp;
      mirror_y = true;
   }

   /* Account for face selection and texture view MinLayer */
   int dst_slice = slice + dst_image->TexObject->MinLayer + dst_image->Face;
   int dst_level = dst_image->Level + dst_image->TexObject->MinLevel;

   brw_blorp_blit_miptrees(brw,
                           src_mt, src_irb->mt_level, src_irb->mt_layer,
                           src_rb->Format, blorp_get_texture_swizzle(src_irb),
                           dst_mt, dst_level, dst_slice,
                           dst_image->TexFormat,
                           srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1,
                           GL_NEAREST, false, mirror_y,
                           false, false);

   /* If we're copying to a packed depth stencil texture and the source
    * framebuffer has separate stencil, we need to also copy the stencil data
    * over.
    */
   src_rb = ctx->ReadBuffer->Attachment[BUFFER_STENCIL].Renderbuffer;
   if (_mesa_get_format_bits(dst_image->TexFormat, GL_STENCIL_BITS) > 0 &&
       src_rb != NULL) {
      src_irb = intel_renderbuffer(src_rb);
      src_mt = src_irb->mt;

      if (src_mt->stencil_mt)
         src_mt = src_mt->stencil_mt;
      if (dst_mt->stencil_mt)
         dst_mt = dst_mt->stencil_mt;

      if (src_mt != dst_mt) {
         brw_blorp_blit_miptrees(brw,
                                 src_mt, src_irb->mt_level, src_irb->mt_layer,
                                 src_mt->format,
                                 blorp_get_texture_swizzle(src_irb),
                                 dst_mt, dst_level, dst_slice,
                                 dst_mt->format,
                                 srcX0, srcY0, srcX1, srcY1,
                                 dstX0, dstY0, dstX1, dstY1,
                                 GL_NEAREST, false, mirror_y,
                                 false, false);
      }
   }

   return true;
}


GLbitfield
brw_blorp_framebuffer(struct brw_context *brw,
                      struct gl_framebuffer *readFb,
                      struct gl_framebuffer *drawFb,
                      GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                      GLbitfield mask, GLenum filter)
{
   /* BLORP is not supported before Gen6. */
   if (brw->gen < 6)
      return mask;

   static GLbitfield buffer_bits[] = {
      GL_COLOR_BUFFER_BIT,
      GL_DEPTH_BUFFER_BIT,
      GL_STENCIL_BUFFER_BIT,
   };

   for (unsigned int i = 0; i < ARRAY_SIZE(buffer_bits); ++i) {
      if ((mask & buffer_bits[i]) &&
       try_blorp_blit(brw, readFb, drawFb,
                      srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      filter, buffer_bits[i])) {
         mask &= ~buffer_bits[i];
      }
   }

   return mask;
}

static bool
set_write_disables(const struct intel_renderbuffer *irb,
                   const GLubyte *color_mask, bool *color_write_disable)
{
   /* Format information in the renderbuffer represents the requirements
    * given by the client. There are cases where the backing miptree uses,
    * for example, RGBA to represent RGBX. Since the client is only expecting
    * RGB we can treat alpha as not used and write whatever we like into it.
    */
   const GLenum base_format = irb->Base.Base._BaseFormat;
   const int components = _mesa_base_format_component_count(base_format);
   bool disables = false;

   assert(components > 0);

   for (int i = 0; i < components; i++) {
      color_write_disable[i] = !color_mask[i];
      disables = disables || !color_mask[i];
   }

   return disables;
}

static unsigned
irb_logical_mt_layer(struct intel_renderbuffer *irb)
{
   return physical_to_logical_layer(irb->mt, irb->mt_layer);
}

static bool
do_single_blorp_clear(struct brw_context *brw, struct gl_framebuffer *fb,
                      struct gl_renderbuffer *rb, unsigned buf,
                      bool partial_clear, bool encode_srgb)
{
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);
   mesa_format format = irb->mt->format;
   uint32_t x0, x1, y0, y1;

   if (!encode_srgb && _mesa_get_format_color_encoding(format) == GL_SRGB)
      format = _mesa_get_srgb_format_linear(format);

   x0 = fb->_Xmin;
   x1 = fb->_Xmax;
   if (rb->Name != 0) {
      y0 = fb->_Ymin;
      y1 = fb->_Ymax;
   } else {
      y0 = rb->Height - fb->_Ymax;
      y1 = rb->Height - fb->_Ymin;
   }

   /* If the clear region is empty, just return. */
   if (x0 == x1 || y0 == y1)
      return true;

   bool can_fast_clear = !partial_clear;

   bool color_write_disable[4] = { false, false, false, false };
   if (set_write_disables(irb, ctx->Color.ColorMask[buf], color_write_disable))
      can_fast_clear = false;

   if (irb->mt->aux_disable & INTEL_AUX_DISABLE_CCS ||
       !brw_is_color_fast_clear_compatible(brw, irb->mt, &ctx->Color.ClearColor))
      can_fast_clear = false;

   const unsigned logical_layer = irb_logical_mt_layer(irb);
   const enum intel_fast_clear_state fast_clear_state =
      intel_miptree_get_fast_clear_state(irb->mt, irb->mt_level,
                                         logical_layer);

   /* Surface state can only record one fast clear color value. Therefore
    * unless different levels/layers agree on the color it can be used to
    * represent only single level/layer. Here it will be reserved for the
    * first slice (level 0, layer 0).
    */
   if (irb->layer_count > 1 || irb->mt_level || irb->mt_layer)
      can_fast_clear = false;

   if (can_fast_clear) {
      union gl_color_union override_color =
         brw_meta_convert_fast_clear_color(brw, irb->mt,
                                           &ctx->Color.ClearColor);

      /* Record the clear color in the miptree so that it will be
       * programmed in SURFACE_STATE by later rendering and resolve
       * operations.
       */
      const bool color_updated = brw_meta_set_fast_clear_color(
                                    brw, &irb->mt->gen9_fast_clear_color,
                                    &override_color);

      /* If the buffer is already in INTEL_FAST_CLEAR_STATE_CLEAR, the clear
       * is redundant and can be skipped.
       */
      if (!color_updated && fast_clear_state == INTEL_FAST_CLEAR_STATE_CLEAR)
         return true;

      /* If the MCS buffer hasn't been allocated yet, we need to allocate
       * it now.
       */
      if (!irb->mt->mcs_buf) {
         assert(!intel_miptree_is_lossless_compressed(brw, irb->mt));
         if (!intel_miptree_alloc_non_msrt_mcs(brw, irb->mt, false)) {
            /* MCS allocation failed--probably this will only happen in
             * out-of-memory conditions.  But in any case, try to recover
             * by falling back to a non-blorp clear technique.
             */
            return false;
         }
      }
   }

   const unsigned num_layers = fb->MaxNumLayers ? irb->layer_count : 1;

   /* We can't setup the blorp_surf until we've allocated the MCS above */
   struct isl_surf isl_tmp[2];
   struct blorp_surf surf;
   unsigned level = irb->mt_level;
   blorp_surf_for_miptree(brw, &surf, irb->mt, true,
                          (1 << ISL_AUX_USAGE_MCS) |
                          (1 << ISL_AUX_USAGE_CCS_E) |
                          (1 << ISL_AUX_USAGE_CCS_D),
                          &level, logical_layer, num_layers, isl_tmp);

   if (can_fast_clear) {
      DBG("%s (fast) to mt %p level %d layers %d+%d\n", __FUNCTION__,
          irb->mt, irb->mt_level, irb->mt_layer, num_layers);

      struct blorp_batch batch;
      blorp_batch_init(&brw->blorp, &batch, brw, 0);
      blorp_fast_clear(&batch, &surf,
                       (enum isl_format)brw->render_target_format[format],
                       level, logical_layer, num_layers,
                       x0, y0, x1, y1);
      blorp_batch_finish(&batch);

      /* Now that the fast clear has occurred, put the buffer in
       * INTEL_FAST_CLEAR_STATE_CLEAR so that we won't waste time doing
       * redundant clears.
       */
      intel_miptree_set_fast_clear_state(brw, irb->mt, irb->mt_level,
                                         logical_layer, num_layers,
                                         INTEL_FAST_CLEAR_STATE_CLEAR);
   } else {
      DBG("%s (slow) to mt %p level %d layer %d+%d\n", __FUNCTION__,
          irb->mt, irb->mt_level, irb->mt_layer, num_layers);

      union isl_color_value clear_color;
      memcpy(clear_color.f32, ctx->Color.ClearColor.f, sizeof(float) * 4);

      struct blorp_batch batch;
      blorp_batch_init(&brw->blorp, &batch, brw, 0);
      blorp_clear(&batch, &surf,
                  (enum isl_format)brw->render_target_format[format],
                  ISL_SWIZZLE_IDENTITY,
                  level, irb_logical_mt_layer(irb), num_layers,
                  x0, y0, x1, y1,
                  clear_color, color_write_disable);
      blorp_batch_finish(&batch);
   }

   /*
    * Ivybrigde PRM Vol 2, Part 1, "11.7 MCS Buffer for Render Target(s)":
    *
    *  Any transition from any value in {Clear, Render, Resolve} to a
    *  different value in {Clear, Render, Resolve} requires end of pipe
    *  synchronization.
    */
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_RENDER_TARGET_FLUSH |
                               PIPE_CONTROL_CS_STALL);

   return true;
}

bool
brw_blorp_clear_color(struct brw_context *brw, struct gl_framebuffer *fb,
                      GLbitfield mask, bool partial_clear, bool encode_srgb)
{
   for (unsigned buf = 0; buf < fb->_NumColorDrawBuffers; buf++) {
      struct gl_renderbuffer *rb = fb->_ColorDrawBuffers[buf];
      struct intel_renderbuffer *irb = intel_renderbuffer(rb);

      /* Only clear the buffers present in the provided mask */
      if (((1 << fb->_ColorDrawBufferIndexes[buf]) & mask) == 0)
         continue;

      /* If this is an ES2 context or GL_ARB_ES2_compatibility is supported,
       * the framebuffer can be complete with some attachments missing.  In
       * this case the _ColorDrawBuffers pointer will be NULL.
       */
      if (rb == NULL)
         continue;

      if (!do_single_blorp_clear(brw, fb, rb, buf, partial_clear,
                                 encode_srgb)) {
         return false;
      }

      irb->need_downsample = true;
   }

   return true;
}

void
brw_blorp_resolve_color(struct brw_context *brw, struct intel_mipmap_tree *mt,
                        unsigned level, unsigned layer)
{
   DBG("%s to mt %p level %u layer %u\n", __FUNCTION__, mt, level, layer);

   const mesa_format format = _mesa_get_srgb_format_linear(mt->format);

   struct isl_surf isl_tmp[2];
   struct blorp_surf surf;
   blorp_surf_for_miptree(brw, &surf, mt, true,
                          (1 << ISL_AUX_USAGE_CCS_E) |
                          (1 << ISL_AUX_USAGE_CCS_D),
                          &level, layer, 1 /* num_layers */,
                          isl_tmp);

   enum blorp_fast_clear_op resolve_op;
   if (brw->gen >= 9) {
      if (surf.aux_usage == ISL_AUX_USAGE_CCS_E)
         resolve_op = BLORP_FAST_CLEAR_OP_RESOLVE_FULL;
      else
         resolve_op = BLORP_FAST_CLEAR_OP_RESOLVE_PARTIAL;
   } else {
      assert(surf.aux_usage == ISL_AUX_USAGE_CCS_D);
      /* Broadwell and earlier do not have a partial resolve */
      resolve_op = BLORP_FAST_CLEAR_OP_RESOLVE_FULL;
   }

   struct blorp_batch batch;
   blorp_batch_init(&brw->blorp, &batch, brw, 0);
   blorp_ccs_resolve(&batch, &surf, level, layer,
                     brw_blorp_to_isl_format(brw, format, true),
                     resolve_op);
   blorp_batch_finish(&batch);

   /*
    * Ivybrigde PRM Vol 2, Part 1, "11.7 MCS Buffer for Render Target(s)":
    *
    *  Any transition from any value in {Clear, Render, Resolve} to a
    *  different value in {Clear, Render, Resolve} requires end of pipe
    *  synchronization.
    */
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_RENDER_TARGET_FLUSH |
                               PIPE_CONTROL_CS_STALL);
}

static void
gen6_blorp_hiz_exec(struct brw_context *brw, struct intel_mipmap_tree *mt,
                    unsigned int level, unsigned int layer, enum blorp_hiz_op op)
{
   assert(intel_miptree_level_has_hiz(mt, level));

   struct isl_surf isl_tmp[2];
   struct blorp_surf surf;
   blorp_surf_for_miptree(brw, &surf, mt, true, (1 << ISL_AUX_USAGE_HIZ),
                          &level, layer, 1, isl_tmp);

   struct blorp_batch batch;
   blorp_batch_init(&brw->blorp, &batch, brw, 0);
   blorp_gen6_hiz_op(&batch, &surf, level, layer, op);
   blorp_batch_finish(&batch);
}

/**
 * Perform a HiZ or depth resolve operation.
 *
 * For an overview of HiZ ops, see the following sections of the Sandy Bridge
 * PRM, Volume 1, Part 2:
 *   - 7.5.3.1 Depth Buffer Clear
 *   - 7.5.3.2 Depth Buffer Resolve
 *   - 7.5.3.3 Hierarchical Depth Buffer Resolve
 */
void
intel_hiz_exec(struct brw_context *brw, struct intel_mipmap_tree *mt,
	       unsigned int level, unsigned int layer, enum blorp_hiz_op op)
{
   const char *opname = NULL;

   switch (op) {
   case BLORP_HIZ_OP_DEPTH_RESOLVE:
      opname = "depth resolve";
      break;
   case BLORP_HIZ_OP_HIZ_RESOLVE:
      opname = "hiz ambiguate";
      break;
   case BLORP_HIZ_OP_DEPTH_CLEAR:
      opname = "depth clear";
      break;
   case BLORP_HIZ_OP_NONE:
      opname = "noop?";
      break;
   }

   DBG("%s %s to mt %p level %d layer %d\n",
       __func__, opname, mt, level, layer);

   if (brw->gen >= 8) {
      gen8_hiz_exec(brw, mt, level, layer, op);
   } else {
      gen6_blorp_hiz_exec(brw, mt, level, layer, op);
   }
}
