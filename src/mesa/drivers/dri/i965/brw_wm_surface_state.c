/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */


#include "compiler/nir/nir.h"
#include "main/context.h"
#include "main/blend.h"
#include "main/mtypes.h"
#include "main/samplerobj.h"
#include "main/shaderimage.h"
#include "main/teximage.h"
#include "program/prog_parameter.h"
#include "program/prog_instruction.h"
#include "main/framebuffer.h"
#include "main/shaderapi.h"

#include "isl/isl.h"

#include "intel_mipmap_tree.h"
#include "intel_batchbuffer.h"
#include "intel_tex.h"
#include "intel_fbo.h"
#include "intel_buffer_objects.h"

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_wm.h"

enum {
   INTEL_RENDERBUFFER_LAYERED = 1 << 0,
   INTEL_AUX_BUFFER_DISABLED = 1 << 1,
};

uint32_t tex_mocs[] = {
   [7] = GEN7_MOCS_L3,
   [8] = BDW_MOCS_WB,
   [9] = SKL_MOCS_WB,
};

uint32_t rb_mocs[] = {
   [7] = GEN7_MOCS_L3,
   [8] = BDW_MOCS_PTE,
   [9] = SKL_MOCS_PTE,
};

static void
brw_emit_surface_state(struct brw_context *brw,
                       struct intel_mipmap_tree *mt, uint32_t flags,
                       GLenum target, struct isl_view view,
                       uint32_t mocs, uint32_t *surf_offset, int surf_index,
                       unsigned read_domains, unsigned write_domains)
{
   uint32_t tile_x = mt->level[0].slice[0].x_offset;
   uint32_t tile_y = mt->level[0].slice[0].y_offset;
   uint32_t offset = mt->offset;

   struct isl_surf surf;
   intel_miptree_get_isl_surf(brw, mt, &surf);

   surf.dim = get_isl_surf_dim(target);

   const enum isl_dim_layout dim_layout =
      get_isl_dim_layout(&brw->screen->devinfo, mt->tiling, target);

   if (surf.dim_layout != dim_layout) {
      /* The layout of the specified texture target is not compatible with the
       * actual layout of the miptree structure in memory -- You're entering
       * dangerous territory, this can only possibly work if you only intended
       * to access a single level and slice of the texture, and the hardware
       * supports the tile offset feature in order to allow non-tile-aligned
       * base offsets, since we'll have to point the hardware to the first
       * texel of the level instead of relying on the usual base level/layer
       * controls.
       */
      assert(brw->has_surface_tile_offset);
      assert(view.levels == 1 && view.array_len == 1);
      assert(tile_x == 0 && tile_y == 0);

      offset += intel_miptree_get_tile_offsets(mt, view.base_level,
                                               view.base_array_layer,
                                               &tile_x, &tile_y);

      /* Minify the logical dimensions of the texture. */
      const unsigned l = view.base_level - mt->first_level;
      surf.logical_level0_px.width = minify(surf.logical_level0_px.width, l);
      surf.logical_level0_px.height = surf.dim <= ISL_SURF_DIM_1D ? 1 :
         minify(surf.logical_level0_px.height, l);
      surf.logical_level0_px.depth = surf.dim <= ISL_SURF_DIM_2D ? 1 :
         minify(surf.logical_level0_px.depth, l);

      /* Only the base level and layer can be addressed with the overridden
       * layout.
       */
      surf.logical_level0_px.array_len = 1;
      surf.levels = 1;
      surf.dim_layout = dim_layout;

      /* The requested slice of the texture is now at the base level and
       * layer.
       */
      view.base_level = 0;
      view.base_array_layer = 0;
   }

   union isl_color_value clear_color = { .u32 = { 0, 0, 0, 0 } };

   drm_intel_bo *aux_bo;
   struct isl_surf *aux_surf = NULL, aux_surf_s;
   uint64_t aux_offset = 0;
   enum isl_aux_usage aux_usage = ISL_AUX_USAGE_NONE;
   if ((mt->mcs_buf || intel_miptree_sample_with_hiz(brw, mt)) &&
       !(flags & INTEL_AUX_BUFFER_DISABLED)) {
      intel_miptree_get_aux_isl_surf(brw, mt, &aux_surf_s, &aux_usage);
      aux_surf = &aux_surf_s;

      if (mt->mcs_buf) {
         aux_bo = mt->mcs_buf->bo;
         aux_offset = mt->mcs_buf->bo->offset64 + mt->mcs_buf->offset;
      } else {
         aux_bo = mt->hiz_buf->aux_base.bo;
         aux_offset = mt->hiz_buf->aux_base.bo->offset64;
      }

      /* We only really need a clear color if we also have an auxiliary
       * surface.  Without one, it does nothing.
       */
      clear_color = intel_miptree_get_isl_clear_color(brw, mt);
   }

   void *state = __brw_state_batch(brw, AUB_TRACE_SURFACE_STATE,
                                   brw->isl_dev.ss.size,
                                   brw->isl_dev.ss.align,
                                   surf_index, surf_offset);

   isl_surf_fill_state(&brw->isl_dev, state, .surf = &surf, .view = &view,
                       .address = mt->bo->offset64 + offset,
                       .aux_surf = aux_surf, .aux_usage = aux_usage,
                       .aux_address = aux_offset,
                       .mocs = mocs, .clear_color = clear_color,
                       .x_offset_sa = tile_x, .y_offset_sa = tile_y);

   drm_intel_bo_emit_reloc(brw->batch.bo,
                           *surf_offset + brw->isl_dev.ss.addr_offset,
                           mt->bo, offset,
                           read_domains, write_domains);

   if (aux_surf) {
      /* On gen7 and prior, the upper 20 bits of surface state DWORD 6 are the
       * upper 20 bits of the GPU address of the MCS buffer; the lower 12 bits
       * contain other control information.  Since buffer addresses are always
       * on 4k boundaries (and thus have their lower 12 bits zero), we can use
       * an ordinary reloc to do the necessary address translation.
       */
      assert((aux_offset & 0xfff) == 0);
      uint32_t *aux_addr = state + brw->isl_dev.ss.aux_addr_offset;
      drm_intel_bo_emit_reloc(brw->batch.bo,
                              *surf_offset + brw->isl_dev.ss.aux_addr_offset,
                              aux_bo, *aux_addr - aux_bo->offset64,
                              read_domains, write_domains);
   }
}

uint32_t
brw_update_renderbuffer_surface(struct brw_context *brw,
                                struct gl_renderbuffer *rb,
                                uint32_t flags, unsigned unit /* unused */,
                                uint32_t surf_index)
{
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);
   struct intel_mipmap_tree *mt = irb->mt;

   if (brw->gen < 9) {
      assert(!(flags & INTEL_AUX_BUFFER_DISABLED));
   }

   assert(brw_render_target_supported(brw, rb));

   mesa_format rb_format = _mesa_get_render_format(ctx, intel_rb_format(irb));
   if (unlikely(!brw->format_supported_as_render_target[rb_format])) {
      _mesa_problem(ctx, "%s: renderbuffer format %s unsupported\n",
                    __func__, _mesa_get_format_name(rb_format));
   }

   const unsigned layer_multiplier =
      (irb->mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
       irb->mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS) ?
      MAX2(irb->mt->num_samples, 1) : 1;

   struct isl_view view = {
      .format = brw->render_target_format[rb_format],
      .base_level = irb->mt_level - irb->mt->first_level,
      .levels = 1,
      .base_array_layer = irb->mt_layer / layer_multiplier,
      .array_len = MAX2(irb->layer_count, 1),
      .swizzle = ISL_SWIZZLE_IDENTITY,
      .usage = ISL_SURF_USAGE_RENDER_TARGET_BIT,
   };

   uint32_t offset;
   brw_emit_surface_state(brw, mt, flags, mt->target, view,
                          rb_mocs[brw->gen],
                          &offset, surf_index,
                          I915_GEM_DOMAIN_RENDER,
                          I915_GEM_DOMAIN_RENDER);
   return offset;
}

GLuint
translate_tex_target(GLenum target)
{
   switch (target) {
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY_EXT:
      return BRW_SURFACE_1D;

   case GL_TEXTURE_RECTANGLE_NV:
      return BRW_SURFACE_2D;

   case GL_TEXTURE_2D:
   case GL_TEXTURE_2D_ARRAY_EXT:
   case GL_TEXTURE_EXTERNAL_OES:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      return BRW_SURFACE_2D;

   case GL_TEXTURE_3D:
      return BRW_SURFACE_3D;

   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
      return BRW_SURFACE_CUBE;

   default:
      unreachable("not reached");
   }
}

uint32_t
brw_get_surface_tiling_bits(uint32_t tiling)
{
   switch (tiling) {
   case I915_TILING_X:
      return BRW_SURFACE_TILED;
   case I915_TILING_Y:
      return BRW_SURFACE_TILED | BRW_SURFACE_TILED_Y;
   default:
      return 0;
   }
}


uint32_t
brw_get_surface_num_multisamples(unsigned num_samples)
{
   if (num_samples > 1)
      return BRW_SURFACE_MULTISAMPLECOUNT_4;
   else
      return BRW_SURFACE_MULTISAMPLECOUNT_1;
}

/**
 * Compute the combination of DEPTH_TEXTURE_MODE and EXT_texture_swizzle
 * swizzling.
 */
int
brw_get_texture_swizzle(const struct gl_context *ctx,
                        const struct gl_texture_object *t)
{
   const struct gl_texture_image *img = t->Image[0][t->BaseLevel];

   int swizzles[SWIZZLE_NIL + 1] = {
      SWIZZLE_X,
      SWIZZLE_Y,
      SWIZZLE_Z,
      SWIZZLE_W,
      SWIZZLE_ZERO,
      SWIZZLE_ONE,
      SWIZZLE_NIL
   };

   if (img->_BaseFormat == GL_DEPTH_COMPONENT ||
       img->_BaseFormat == GL_DEPTH_STENCIL) {
      GLenum depth_mode = t->DepthMode;

      /* In ES 3.0, DEPTH_TEXTURE_MODE is expected to be GL_RED for textures
       * with depth component data specified with a sized internal format.
       * Otherwise, it's left at the old default, GL_LUMINANCE.
       */
      if (_mesa_is_gles3(ctx) &&
          img->InternalFormat != GL_DEPTH_COMPONENT &&
          img->InternalFormat != GL_DEPTH_STENCIL) {
         depth_mode = GL_RED;
      }

      switch (depth_mode) {
      case GL_ALPHA:
         swizzles[0] = SWIZZLE_ZERO;
         swizzles[1] = SWIZZLE_ZERO;
         swizzles[2] = SWIZZLE_ZERO;
         swizzles[3] = SWIZZLE_X;
         break;
      case GL_LUMINANCE:
         swizzles[0] = SWIZZLE_X;
         swizzles[1] = SWIZZLE_X;
         swizzles[2] = SWIZZLE_X;
         swizzles[3] = SWIZZLE_ONE;
         break;
      case GL_INTENSITY:
         swizzles[0] = SWIZZLE_X;
         swizzles[1] = SWIZZLE_X;
         swizzles[2] = SWIZZLE_X;
         swizzles[3] = SWIZZLE_X;
         break;
      case GL_RED:
         swizzles[0] = SWIZZLE_X;
         swizzles[1] = SWIZZLE_ZERO;
         swizzles[2] = SWIZZLE_ZERO;
         swizzles[3] = SWIZZLE_ONE;
         break;
      }
   }

   GLenum datatype = _mesa_get_format_datatype(img->TexFormat);

   /* If the texture's format is alpha-only, force R, G, and B to
    * 0.0. Similarly, if the texture's format has no alpha channel,
    * force the alpha value read to 1.0. This allows for the
    * implementation to use an RGBA texture for any of these formats
    * without leaking any unexpected values.
    */
   switch (img->_BaseFormat) {
   case GL_ALPHA:
      swizzles[0] = SWIZZLE_ZERO;
      swizzles[1] = SWIZZLE_ZERO;
      swizzles[2] = SWIZZLE_ZERO;
      break;
   case GL_LUMINANCE:
      if (t->_IsIntegerFormat || datatype == GL_SIGNED_NORMALIZED) {
         swizzles[0] = SWIZZLE_X;
         swizzles[1] = SWIZZLE_X;
         swizzles[2] = SWIZZLE_X;
         swizzles[3] = SWIZZLE_ONE;
      }
      break;
   case GL_LUMINANCE_ALPHA:
      if (datatype == GL_SIGNED_NORMALIZED) {
         swizzles[0] = SWIZZLE_X;
         swizzles[1] = SWIZZLE_X;
         swizzles[2] = SWIZZLE_X;
         swizzles[3] = SWIZZLE_W;
      }
      break;
   case GL_INTENSITY:
      if (datatype == GL_SIGNED_NORMALIZED) {
         swizzles[0] = SWIZZLE_X;
         swizzles[1] = SWIZZLE_X;
         swizzles[2] = SWIZZLE_X;
         swizzles[3] = SWIZZLE_X;
      }
      break;
   case GL_RED:
   case GL_RG:
   case GL_RGB:
      if (_mesa_get_format_bits(img->TexFormat, GL_ALPHA_BITS) > 0)
         swizzles[3] = SWIZZLE_ONE;
      break;
   }

   return MAKE_SWIZZLE4(swizzles[GET_SWZ(t->_Swizzle, 0)],
                        swizzles[GET_SWZ(t->_Swizzle, 1)],
                        swizzles[GET_SWZ(t->_Swizzle, 2)],
                        swizzles[GET_SWZ(t->_Swizzle, 3)]);
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
static unsigned
swizzle_to_scs(GLenum swizzle, bool need_green_to_blue)
{
   unsigned scs = (swizzle + 4) & 7;

   return (need_green_to_blue && scs == HSW_SCS_GREEN) ? HSW_SCS_BLUE : scs;
}

static unsigned
brw_find_matching_rb(const struct gl_framebuffer *fb,
                     const struct intel_mipmap_tree *mt)
{
   for (unsigned i = 0; i < fb->_NumColorDrawBuffers; i++) {
      const struct intel_renderbuffer *irb =
         intel_renderbuffer(fb->_ColorDrawBuffers[i]);

      if (irb && irb->mt == mt)
         return i;
   }

   return fb->_NumColorDrawBuffers;
}

static inline bool
brw_texture_view_sane(const struct brw_context *brw,
                      const struct intel_mipmap_tree *mt,
                      const struct isl_view *view)
{
   /* There are special cases only for lossless compression. */
   if (!intel_miptree_is_lossless_compressed(brw, mt))
      return true;

   if (isl_format_supports_lossless_compression(&brw->screen->devinfo,
                                                view->format))
      return true;

   /* Logic elsewhere needs to take care to resolve the color buffer prior
    * to sampling it as non-compressed.
    */
   if (intel_miptree_has_color_unresolved(mt, view->base_level, view->levels,
                                          view->base_array_layer,
                                          view->array_len))
      return false;

   const struct gl_framebuffer *fb = brw->ctx.DrawBuffer;
   const unsigned rb_index = brw_find_matching_rb(fb, mt);

   if (rb_index == fb->_NumColorDrawBuffers)
      return true;

   /* Underlying surface is compressed but it is sampled using a format that
    * the sampling engine doesn't support as compressed. Compression must be
    * disabled for both sampling engine and data port in case the same surface
    * is used also as render target.
    */
   return brw->draw_aux_buffer_disabled[rb_index];
}

static bool
brw_disable_aux_surface(const struct brw_context *brw,
                        const struct intel_mipmap_tree *mt,
                        const struct isl_view *view)
{
   /* Nothing to disable. */
   if (!mt->mcs_buf)
      return false;

   const bool is_unresolved = intel_miptree_has_color_unresolved(
                                 mt, view->base_level, view->levels,
                                 view->base_array_layer, view->array_len);

   /* There are special cases only for lossless compression. */
   if (!intel_miptree_is_lossless_compressed(brw, mt))
      return !is_unresolved;

   const struct gl_framebuffer *fb = brw->ctx.DrawBuffer;
   const unsigned rb_index = brw_find_matching_rb(fb, mt);

   /* If we are drawing into this with compression enabled, then we must also
    * enable compression when texturing from it regardless of
    * fast_clear_state.  If we don't then, after the first draw call with
    * this setup, there will be data in the CCS which won't get picked up by
    * subsequent texturing operations as required by ARB_texture_barrier.
    * Since we don't want to re-emit the binding table or do a resolve
    * operation every draw call, the easiest thing to do is just enable
    * compression on the texturing side.  This is completely safe to do
    * since, if compressed texturing weren't allowed, we would have disabled
    * compression of render targets in whatever_that_function_is_called().
    */
   if (rb_index < fb->_NumColorDrawBuffers) {
      if (brw->draw_aux_buffer_disabled[rb_index]) {
         assert(!is_unresolved);
      }

      return brw->draw_aux_buffer_disabled[rb_index];
   }

   return !is_unresolved;
}

void
brw_update_texture_surface(struct gl_context *ctx,
                           unsigned unit,
                           uint32_t *surf_offset,
                           bool for_gather,
                           uint32_t plane)
{
   struct brw_context *brw = brw_context(ctx);
   struct gl_texture_object *obj = ctx->Texture.Unit[unit]._Current;

   if (obj->Target == GL_TEXTURE_BUFFER) {
      brw_update_buffer_texture_surface(ctx, unit, surf_offset);

   } else {
      struct intel_texture_object *intel_obj = intel_texture_object(obj);
      struct intel_mipmap_tree *mt = intel_obj->mt;

      if (plane > 0) {
         if (mt->plane[plane - 1] == NULL)
            return;
         mt = mt->plane[plane - 1];
      }

      struct gl_sampler_object *sampler = _mesa_get_samplerobj(ctx, unit);
      /* If this is a view with restricted NumLayers, then our effective depth
       * is not just the miptree depth.
       */
      const unsigned view_num_layers =
         (obj->Immutable && obj->Target != GL_TEXTURE_3D) ? obj->NumLayers :
                                                            mt->logical_depth0;

      /* Handling GL_ALPHA as a surface format override breaks 1.30+ style
       * texturing functions that return a float, as our code generation always
       * selects the .x channel (which would always be 0).
       */
      struct gl_texture_image *firstImage = obj->Image[0][obj->BaseLevel];
      const bool alpha_depth = obj->DepthMode == GL_ALPHA &&
         (firstImage->_BaseFormat == GL_DEPTH_COMPONENT ||
          firstImage->_BaseFormat == GL_DEPTH_STENCIL);
      const unsigned swizzle = (unlikely(alpha_depth) ? SWIZZLE_XYZW :
                                brw_get_texture_swizzle(&brw->ctx, obj));

      mesa_format mesa_fmt = plane == 0 ? intel_obj->_Format : mt->format;
      unsigned format = translate_tex_format(brw, mesa_fmt,
                                             sampler->sRGBDecode);

      /* Implement gen6 and gen7 gather work-around */
      bool need_green_to_blue = false;
      if (for_gather) {
         if (brw->gen == 7 && (format == BRW_SURFACEFORMAT_R32G32_FLOAT ||
                               format == BRW_SURFACEFORMAT_R32G32_SINT ||
                               format == BRW_SURFACEFORMAT_R32G32_UINT)) {
            format = BRW_SURFACEFORMAT_R32G32_FLOAT_LD;
            need_green_to_blue = brw->is_haswell;
         } else if (brw->gen == 6) {
            /* Sandybridge's gather4 message is broken for integer formats.
             * To work around this, we pretend the surface is UNORM for
             * 8 or 16-bit formats, and emit shader instructions to recover
             * the real INT/UINT value.  For 32-bit formats, we pretend
             * the surface is FLOAT, and simply reinterpret the resulting
             * bits.
             */
            switch (format) {
            case BRW_SURFACEFORMAT_R8_SINT:
            case BRW_SURFACEFORMAT_R8_UINT:
               format = BRW_SURFACEFORMAT_R8_UNORM;
               break;

            case BRW_SURFACEFORMAT_R16_SINT:
            case BRW_SURFACEFORMAT_R16_UINT:
               format = BRW_SURFACEFORMAT_R16_UNORM;
               break;

            case BRW_SURFACEFORMAT_R32_SINT:
            case BRW_SURFACEFORMAT_R32_UINT:
               format = BRW_SURFACEFORMAT_R32_FLOAT;
               break;

            default:
               break;
            }
         }
      }

      if (obj->StencilSampling && firstImage->_BaseFormat == GL_DEPTH_STENCIL) {
         if (brw->gen <= 7) {
            assert(mt->r8stencil_mt && !mt->stencil_mt->r8stencil_needs_update);
            mt = mt->r8stencil_mt;
         } else {
            mt = mt->stencil_mt;
         }
         format = BRW_SURFACEFORMAT_R8_UINT;
      } else if (brw->gen <= 7 && mt->format == MESA_FORMAT_S_UINT8) {
         assert(mt->r8stencil_mt && !mt->r8stencil_needs_update);
         mt = mt->r8stencil_mt;
         format = BRW_SURFACEFORMAT_R8_UINT;
      }

      const int surf_index = surf_offset - &brw->wm.base.surf_offset[0];

      struct isl_view view = {
         .format = format,
         .base_level = obj->MinLevel + obj->BaseLevel,
         .levels = intel_obj->_MaxLevel - obj->BaseLevel + 1,
         .base_array_layer = obj->MinLayer,
         .array_len = view_num_layers,
         .swizzle = {
            .r = swizzle_to_scs(GET_SWZ(swizzle, 0), need_green_to_blue),
            .g = swizzle_to_scs(GET_SWZ(swizzle, 1), need_green_to_blue),
            .b = swizzle_to_scs(GET_SWZ(swizzle, 2), need_green_to_blue),
            .a = swizzle_to_scs(GET_SWZ(swizzle, 3), need_green_to_blue),
         },
         .usage = ISL_SURF_USAGE_TEXTURE_BIT,
      };

      if (obj->Target == GL_TEXTURE_CUBE_MAP ||
          obj->Target == GL_TEXTURE_CUBE_MAP_ARRAY)
         view.usage |= ISL_SURF_USAGE_CUBE_BIT;

      assert(brw_texture_view_sane(brw, mt, &view));

      const int flags = brw_disable_aux_surface(brw, mt, &view) ?
                           INTEL_AUX_BUFFER_DISABLED : 0;
      brw_emit_surface_state(brw, mt, flags, mt->target, view,
                             tex_mocs[brw->gen],
                             surf_offset, surf_index,
                             I915_GEM_DOMAIN_SAMPLER, 0);
   }
}

void
brw_emit_buffer_surface_state(struct brw_context *brw,
                              uint32_t *out_offset,
                              drm_intel_bo *bo,
                              unsigned buffer_offset,
                              unsigned surface_format,
                              unsigned buffer_size,
                              unsigned pitch,
                              bool rw)
{
   uint32_t *dw = brw_state_batch(brw, AUB_TRACE_SURFACE_STATE,
                                  brw->isl_dev.ss.size,
                                  brw->isl_dev.ss.align,
                                  out_offset);

   isl_buffer_fill_state(&brw->isl_dev, dw,
                         .address = (bo ? bo->offset64 : 0) + buffer_offset,
                         .size = buffer_size,
                         .format = surface_format,
                         .stride = pitch,
                         .mocs = tex_mocs[brw->gen]);

   if (bo) {
      drm_intel_bo_emit_reloc(brw->batch.bo,
                              *out_offset + brw->isl_dev.ss.addr_offset,
                              bo, buffer_offset,
                              I915_GEM_DOMAIN_SAMPLER,
                              (rw ? I915_GEM_DOMAIN_SAMPLER : 0));
   }
}

void
brw_update_buffer_texture_surface(struct gl_context *ctx,
                                  unsigned unit,
                                  uint32_t *surf_offset)
{
   struct brw_context *brw = brw_context(ctx);
   struct gl_texture_object *tObj = ctx->Texture.Unit[unit]._Current;
   struct intel_buffer_object *intel_obj =
      intel_buffer_object(tObj->BufferObject);
   uint32_t size = tObj->BufferSize;
   drm_intel_bo *bo = NULL;
   mesa_format format = tObj->_BufferObjectFormat;
   uint32_t brw_format = brw_format_for_mesa_format(format);
   int texel_size = _mesa_get_format_bytes(format);

   if (intel_obj) {
      size = MIN2(size, intel_obj->Base.Size);
      bo = intel_bufferobj_buffer(brw, intel_obj, tObj->BufferOffset, size);
   }

   if (brw_format == 0 && format != MESA_FORMAT_RGBA_FLOAT32) {
      _mesa_problem(NULL, "bad format %s for texture buffer\n",
		    _mesa_get_format_name(format));
   }

   brw_emit_buffer_surface_state(brw, surf_offset, bo,
                                 tObj->BufferOffset,
                                 brw_format,
                                 size,
                                 texel_size,
                                 false /* rw */);
}

/**
 * Create the constant buffer surface.  Vertex/fragment shader constants will be
 * read from this buffer with Data Port Read instructions/messages.
 */
void
brw_create_constant_surface(struct brw_context *brw,
			    drm_intel_bo *bo,
			    uint32_t offset,
			    uint32_t size,
			    uint32_t *out_offset)
{
   brw_emit_buffer_surface_state(brw, out_offset, bo, offset,
                                 BRW_SURFACEFORMAT_R32G32B32A32_FLOAT,
                                 size, 1, false);
}

/**
 * Create the buffer surface. Shader buffer variables will be
 * read from / write to this buffer with Data Port Read/Write
 * instructions/messages.
 */
void
brw_create_buffer_surface(struct brw_context *brw,
                          drm_intel_bo *bo,
                          uint32_t offset,
                          uint32_t size,
                          uint32_t *out_offset)
{
   /* Use a raw surface so we can reuse existing untyped read/write/atomic
    * messages. We need these specifically for the fragment shader since they
    * include a pixel mask header that we need to ensure correct behavior
    * with helper invocations, which cannot write to the buffer.
    */
   brw_emit_buffer_surface_state(brw, out_offset, bo, offset,
                                 BRW_SURFACEFORMAT_RAW,
                                 size, 1, true);
}

/**
 * Set up a binding table entry for use by stream output logic (transform
 * feedback).
 *
 * buffer_size_minus_1 must be less than BRW_MAX_NUM_BUFFER_ENTRIES.
 */
void
brw_update_sol_surface(struct brw_context *brw,
                       struct gl_buffer_object *buffer_obj,
                       uint32_t *out_offset, unsigned num_vector_components,
                       unsigned stride_dwords, unsigned offset_dwords)
{
   struct intel_buffer_object *intel_bo = intel_buffer_object(buffer_obj);
   uint32_t offset_bytes = 4 * offset_dwords;
   drm_intel_bo *bo = intel_bufferobj_buffer(brw, intel_bo,
                                             offset_bytes,
                                             buffer_obj->Size - offset_bytes);
   uint32_t *surf = brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 6 * 4, 32,
                                    out_offset);
   uint32_t pitch_minus_1 = 4*stride_dwords - 1;
   size_t size_dwords = buffer_obj->Size / 4;
   uint32_t buffer_size_minus_1, width, height, depth, surface_format;

   /* FIXME: can we rely on core Mesa to ensure that the buffer isn't
    * too big to map using a single binding table entry?
    */
   assert((size_dwords - offset_dwords) / stride_dwords
          <= BRW_MAX_NUM_BUFFER_ENTRIES);

   if (size_dwords > offset_dwords + num_vector_components) {
      /* There is room for at least 1 transform feedback output in the buffer.
       * Compute the number of additional transform feedback outputs the
       * buffer has room for.
       */
      buffer_size_minus_1 =
         (size_dwords - offset_dwords - num_vector_components) / stride_dwords;
   } else {
      /* There isn't even room for a single transform feedback output in the
       * buffer.  We can't configure the binding table entry to prevent output
       * entirely; we'll have to rely on the geometry shader to detect
       * overflow.  But to minimize the damage in case of a bug, set up the
       * binding table entry to just allow a single output.
       */
      buffer_size_minus_1 = 0;
   }
   width = buffer_size_minus_1 & 0x7f;
   height = (buffer_size_minus_1 & 0xfff80) >> 7;
   depth = (buffer_size_minus_1 & 0x7f00000) >> 20;

   switch (num_vector_components) {
   case 1:
      surface_format = BRW_SURFACEFORMAT_R32_FLOAT;
      break;
   case 2:
      surface_format = BRW_SURFACEFORMAT_R32G32_FLOAT;
      break;
   case 3:
      surface_format = BRW_SURFACEFORMAT_R32G32B32_FLOAT;
      break;
   case 4:
      surface_format = BRW_SURFACEFORMAT_R32G32B32A32_FLOAT;
      break;
   default:
      unreachable("Invalid vector size for transform feedback output");
   }

   surf[0] = BRW_SURFACE_BUFFER << BRW_SURFACE_TYPE_SHIFT |
      BRW_SURFACE_MIPMAPLAYOUT_BELOW << BRW_SURFACE_MIPLAYOUT_SHIFT |
      surface_format << BRW_SURFACE_FORMAT_SHIFT |
      BRW_SURFACE_RC_READ_WRITE;
   surf[1] = bo->offset64 + offset_bytes; /* reloc */
   surf[2] = (width << BRW_SURFACE_WIDTH_SHIFT |
	      height << BRW_SURFACE_HEIGHT_SHIFT);
   surf[3] = (depth << BRW_SURFACE_DEPTH_SHIFT |
              pitch_minus_1 << BRW_SURFACE_PITCH_SHIFT);
   surf[4] = 0;
   surf[5] = 0;

   /* Emit relocation to surface contents. */
   drm_intel_bo_emit_reloc(brw->batch.bo,
			   *out_offset + 4,
			   bo, offset_bytes,
			   I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER);
}

/* Creates a new WM constant buffer reflecting the current fragment program's
 * constants, if needed by the fragment program.
 *
 * Otherwise, constants go through the CURBEs using the brw_constant_buffer
 * state atom.
 */
static void
brw_upload_wm_pull_constants(struct brw_context *brw)
{
   struct brw_stage_state *stage_state = &brw->wm.base;
   /* BRW_NEW_FRAGMENT_PROGRAM */
   struct brw_program *fp = (struct brw_program *) brw->fragment_program;
   /* BRW_NEW_FS_PROG_DATA */
   struct brw_stage_prog_data *prog_data = brw->wm.base.prog_data;

   _mesa_shader_write_subroutine_indices(&brw->ctx, MESA_SHADER_FRAGMENT);
   /* _NEW_PROGRAM_CONSTANTS */
   brw_upload_pull_constants(brw, BRW_NEW_SURFACES, &fp->program,
                             stage_state, prog_data);
}

const struct brw_tracked_state brw_wm_pull_constants = {
   .dirty = {
      .mesa = _NEW_PROGRAM_CONSTANTS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_FRAGMENT_PROGRAM |
             BRW_NEW_FS_PROG_DATA,
   },
   .emit = brw_upload_wm_pull_constants,
};

/**
 * Creates a null renderbuffer surface.
 *
 * This is used when the shader doesn't write to any color output.  An FB
 * write to target 0 will still be emitted, because that's how the thread is
 * terminated (and computed depth is returned), so we need to have the
 * hardware discard the target 0 color output..
 */
static void
brw_emit_null_surface_state(struct brw_context *brw,
                            unsigned width,
                            unsigned height,
                            unsigned samples,
                            uint32_t *out_offset)
{
   /* From the Sandy bridge PRM, Vol4 Part1 p71 (Surface Type: Programming
    * Notes):
    *
    *     A null surface will be used in instances where an actual surface is
    *     not bound. When a write message is generated to a null surface, no
    *     actual surface is written to. When a read message (including any
    *     sampling engine message) is generated to a null surface, the result
    *     is all zeros. Note that a null surface type is allowed to be used
    *     with all messages, even if it is not specificially indicated as
    *     supported. All of the remaining fields in surface state are ignored
    *     for null surfaces, with the following exceptions:
    *
    *     - [DevSNB+]: Width, Height, Depth, and LOD fields must match the
    *       depth buffer’s corresponding state for all render target surfaces,
    *       including null.
    *
    *     - Surface Format must be R8G8B8A8_UNORM.
    */
   unsigned surface_type = BRW_SURFACE_NULL;
   drm_intel_bo *bo = NULL;
   unsigned pitch_minus_1 = 0;
   uint32_t multisampling_state = 0;
   uint32_t *surf = brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 6 * 4, 32,
                                    out_offset);

   if (samples > 1) {
      /* On Gen6, null render targets seem to cause GPU hangs when
       * multisampling.  So work around this problem by rendering into dummy
       * color buffer.
       *
       * To decrease the amount of memory needed by the workaround buffer, we
       * set its pitch to 128 bytes (the width of a Y tile).  This means that
       * the amount of memory needed for the workaround buffer is
       * (width_in_tiles + height_in_tiles - 1) tiles.
       *
       * Note that since the workaround buffer will be interpreted by the
       * hardware as an interleaved multisampled buffer, we need to compute
       * width_in_tiles and height_in_tiles by dividing the width and height
       * by 16 rather than the normal Y-tile size of 32.
       */
      unsigned width_in_tiles = ALIGN(width, 16) / 16;
      unsigned height_in_tiles = ALIGN(height, 16) / 16;
      unsigned size_needed = (width_in_tiles + height_in_tiles - 1) * 4096;
      brw_get_scratch_bo(brw, &brw->wm.multisampled_null_render_target_bo,
                         size_needed);
      bo = brw->wm.multisampled_null_render_target_bo;
      surface_type = BRW_SURFACE_2D;
      pitch_minus_1 = 127;
      multisampling_state = brw_get_surface_num_multisamples(samples);
   }

   surf[0] = (surface_type << BRW_SURFACE_TYPE_SHIFT |
	      BRW_SURFACEFORMAT_B8G8R8A8_UNORM << BRW_SURFACE_FORMAT_SHIFT);
   if (brw->gen < 6) {
      surf[0] |= (1 << BRW_SURFACE_WRITEDISABLE_R_SHIFT |
		  1 << BRW_SURFACE_WRITEDISABLE_G_SHIFT |
		  1 << BRW_SURFACE_WRITEDISABLE_B_SHIFT |
		  1 << BRW_SURFACE_WRITEDISABLE_A_SHIFT);
   }
   surf[1] = bo ? bo->offset64 : 0;
   surf[2] = ((width - 1) << BRW_SURFACE_WIDTH_SHIFT |
              (height - 1) << BRW_SURFACE_HEIGHT_SHIFT);

   /* From Sandy bridge PRM, Vol4 Part1 p82 (Tiled Surface: Programming
    * Notes):
    *
    *     If Surface Type is SURFTYPE_NULL, this field must be TRUE
    */
   surf[3] = (BRW_SURFACE_TILED | BRW_SURFACE_TILED_Y |
              pitch_minus_1 << BRW_SURFACE_PITCH_SHIFT);
   surf[4] = multisampling_state;
   surf[5] = 0;

   if (bo) {
      drm_intel_bo_emit_reloc(brw->batch.bo,
                              *out_offset + 4,
                              bo, 0,
                              I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER);
   }
}

/**
 * Sets up a surface state structure to point at the given region.
 * While it is only used for the front/back buffer currently, it should be
 * usable for further buffers when doing ARB_draw_buffer support.
 */
static uint32_t
gen4_update_renderbuffer_surface(struct brw_context *brw,
                                 struct gl_renderbuffer *rb,
                                 uint32_t flags, unsigned unit,
                                 uint32_t surf_index)
{
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);
   struct intel_mipmap_tree *mt = irb->mt;
   uint32_t *surf;
   uint32_t tile_x, tile_y;
   uint32_t format = 0;
   uint32_t offset;
   /* _NEW_BUFFERS */
   mesa_format rb_format = _mesa_get_render_format(ctx, intel_rb_format(irb));
   /* BRW_NEW_FS_PROG_DATA */

   assert(!(flags & INTEL_RENDERBUFFER_LAYERED));
   assert(!(flags & INTEL_AUX_BUFFER_DISABLED));

   if (rb->TexImage && !brw->has_surface_tile_offset) {
      intel_renderbuffer_get_tile_offsets(irb, &tile_x, &tile_y);

      if (tile_x != 0 || tile_y != 0) {
	 /* Original gen4 hardware couldn't draw to a non-tile-aligned
	  * destination in a miptree unless you actually setup your renderbuffer
	  * as a miptree and used the fragile lod/array_index/etc. controls to
	  * select the image.  So, instead, we just make a new single-level
	  * miptree and render into that.
	  */
	 intel_renderbuffer_move_to_temp(brw, irb, false);
	 mt = irb->mt;
      }
   }

   surf = brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 6 * 4, 32, &offset);

   format = brw->render_target_format[rb_format];
   if (unlikely(!brw->format_supported_as_render_target[rb_format])) {
      _mesa_problem(ctx, "%s: renderbuffer format %s unsupported\n",
                    __func__, _mesa_get_format_name(rb_format));
   }

   surf[0] = (BRW_SURFACE_2D << BRW_SURFACE_TYPE_SHIFT |
	      format << BRW_SURFACE_FORMAT_SHIFT);

   /* reloc */
   assert(mt->offset % mt->cpp == 0);
   surf[1] = (intel_renderbuffer_get_tile_offsets(irb, &tile_x, &tile_y) +
	      mt->bo->offset64 + mt->offset);

   surf[2] = ((rb->Width - 1) << BRW_SURFACE_WIDTH_SHIFT |
	      (rb->Height - 1) << BRW_SURFACE_HEIGHT_SHIFT);

   surf[3] = (brw_get_surface_tiling_bits(mt->tiling) |
	      (mt->pitch - 1) << BRW_SURFACE_PITCH_SHIFT);

   surf[4] = brw_get_surface_num_multisamples(mt->num_samples);

   assert(brw->has_surface_tile_offset || (tile_x == 0 && tile_y == 0));
   /* Note that the low bits of these fields are missing, so
    * there's the possibility of getting in trouble.
    */
   assert(tile_x % 4 == 0);
   assert(tile_y % 2 == 0);
   surf[5] = ((tile_x / 4) << BRW_SURFACE_X_OFFSET_SHIFT |
	      (tile_y / 2) << BRW_SURFACE_Y_OFFSET_SHIFT |
	      (mt->valign == 4 ? BRW_SURFACE_VERTICAL_ALIGN_ENABLE : 0));

   if (brw->gen < 6) {
      /* _NEW_COLOR */
      if (!ctx->Color.ColorLogicOpEnabled && !ctx->Color._AdvancedBlendMode &&
          (ctx->Color.BlendEnabled & (1 << unit)))
	 surf[0] |= BRW_SURFACE_BLEND_ENABLED;

      if (!ctx->Color.ColorMask[unit][0])
	 surf[0] |= 1 << BRW_SURFACE_WRITEDISABLE_R_SHIFT;
      if (!ctx->Color.ColorMask[unit][1])
	 surf[0] |= 1 << BRW_SURFACE_WRITEDISABLE_G_SHIFT;
      if (!ctx->Color.ColorMask[unit][2])
	 surf[0] |= 1 << BRW_SURFACE_WRITEDISABLE_B_SHIFT;

      /* As mentioned above, disable writes to the alpha component when the
       * renderbuffer is XRGB.
       */
      if (ctx->DrawBuffer->Visual.alphaBits == 0 ||
	  !ctx->Color.ColorMask[unit][3]) {
	 surf[0] |= 1 << BRW_SURFACE_WRITEDISABLE_A_SHIFT;
      }
   }

   drm_intel_bo_emit_reloc(brw->batch.bo,
                           offset + 4,
                           mt->bo,
                           surf[1] - mt->bo->offset64,
                           I915_GEM_DOMAIN_RENDER,
                           I915_GEM_DOMAIN_RENDER);

   return offset;
}

/**
 * Construct SURFACE_STATE objects for renderbuffers/draw buffers.
 */
void
brw_update_renderbuffer_surfaces(struct brw_context *brw,
                                 const struct gl_framebuffer *fb,
                                 uint32_t render_target_start,
                                 uint32_t *surf_offset)
{
   GLuint i;
   const unsigned int w = _mesa_geometric_width(fb);
   const unsigned int h = _mesa_geometric_height(fb);
   const unsigned int s = _mesa_geometric_samples(fb);

   /* Update surfaces for drawing buffers */
   if (fb->_NumColorDrawBuffers >= 1) {
      for (i = 0; i < fb->_NumColorDrawBuffers; i++) {
         const uint32_t surf_index = render_target_start + i;
         const int flags = (_mesa_geometric_layers(fb) > 0 ?
                              INTEL_RENDERBUFFER_LAYERED : 0) |
                           (brw->draw_aux_buffer_disabled[i] ?
                              INTEL_AUX_BUFFER_DISABLED : 0);

	 if (intel_renderbuffer(fb->_ColorDrawBuffers[i])) {
            surf_offset[surf_index] =
               brw->vtbl.update_renderbuffer_surface(
                  brw, fb->_ColorDrawBuffers[i], flags, i, surf_index);
	 } else {
            brw->vtbl.emit_null_surface_state(brw, w, h, s,
               &surf_offset[surf_index]);
	 }
      }
   } else {
      const uint32_t surf_index = render_target_start;
      brw->vtbl.emit_null_surface_state(brw, w, h, s,
         &surf_offset[surf_index]);
   }
}

static void
update_renderbuffer_surfaces(struct brw_context *brw)
{
   const struct gl_context *ctx = &brw->ctx;

   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(brw->wm.base.prog_data);

   /* _NEW_BUFFERS | _NEW_COLOR */
   const struct gl_framebuffer *fb = ctx->DrawBuffer;
   brw_update_renderbuffer_surfaces(
      brw, fb,
      wm_prog_data->binding_table.render_target_start,
      brw->wm.base.surf_offset);
   brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
}

const struct brw_tracked_state brw_renderbuffer_surfaces = {
   .dirty = {
      .mesa = _NEW_BUFFERS |
              _NEW_COLOR,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_FS_PROG_DATA,
   },
   .emit = update_renderbuffer_surfaces,
};

const struct brw_tracked_state gen6_renderbuffer_surfaces = {
   .dirty = {
      .mesa = _NEW_BUFFERS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP,
   },
   .emit = update_renderbuffer_surfaces,
};

static void
update_renderbuffer_read_surfaces(struct brw_context *brw)
{
   const struct gl_context *ctx = &brw->ctx;

   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(brw->wm.base.prog_data);

   /* BRW_NEW_FRAGMENT_PROGRAM */
   if (!ctx->Extensions.MESA_shader_framebuffer_fetch &&
       brw->fragment_program && brw->fragment_program->info.outputs_read) {
      /* _NEW_BUFFERS */
      const struct gl_framebuffer *fb = ctx->DrawBuffer;

      for (unsigned i = 0; i < fb->_NumColorDrawBuffers; i++) {
         struct gl_renderbuffer *rb = fb->_ColorDrawBuffers[i];
         const struct intel_renderbuffer *irb = intel_renderbuffer(rb);
         const unsigned surf_index =
            wm_prog_data->binding_table.render_target_read_start + i;
         uint32_t *surf_offset = &brw->wm.base.surf_offset[surf_index];

         if (irb) {
            const unsigned format = brw->render_target_format[
               _mesa_get_render_format(ctx, intel_rb_format(irb))];
            assert(isl_format_supports_sampling(&brw->screen->devinfo,
                                                format));

            /* Override the target of the texture if the render buffer is a
             * single slice of a 3D texture (since the minimum array element
             * field of the surface state structure is ignored by the sampler
             * unit for 3D textures on some hardware), or if the render buffer
             * is a 1D array (since shaders always provide the array index
             * coordinate at the Z component to avoid state-dependent
             * recompiles when changing the texture target of the
             * framebuffer).
             */
            const GLenum target =
               (irb->mt->target == GL_TEXTURE_3D &&
                irb->layer_count == 1) ? GL_TEXTURE_2D :
               irb->mt->target == GL_TEXTURE_1D_ARRAY ? GL_TEXTURE_2D_ARRAY :
               irb->mt->target;

            /* intel_renderbuffer::mt_layer is expressed in sample units for
             * the UMS and CMS multisample layouts, but
             * intel_renderbuffer::layer_count is expressed in units of whole
             * logical layers regardless of the multisample layout.
             */
            const unsigned mt_layer_unit =
               (irb->mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
                irb->mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS) ?
               MAX2(irb->mt->num_samples, 1) : 1;

            const struct isl_view view = {
               .format = format,
               .base_level = irb->mt_level - irb->mt->first_level,
               .levels = 1,
               .base_array_layer = irb->mt_layer / mt_layer_unit,
               .array_len = irb->layer_count,
               .swizzle = ISL_SWIZZLE_IDENTITY,
               .usage = ISL_SURF_USAGE_TEXTURE_BIT,
            };

            const int flags = brw->draw_aux_buffer_disabled[i] ?
                                 INTEL_AUX_BUFFER_DISABLED : 0;
            brw_emit_surface_state(brw, irb->mt, flags, target, view,
                                   tex_mocs[brw->gen],
                                   surf_offset, surf_index,
                                   I915_GEM_DOMAIN_SAMPLER, 0);

         } else {
            brw->vtbl.emit_null_surface_state(
               brw, _mesa_geometric_width(fb), _mesa_geometric_height(fb),
               _mesa_geometric_samples(fb), surf_offset);
         }
      }

      brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
   }
}

const struct brw_tracked_state brw_renderbuffer_read_surfaces = {
   .dirty = {
      .mesa = _NEW_BUFFERS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_FRAGMENT_PROGRAM |
             BRW_NEW_FS_PROG_DATA,
   },
   .emit = update_renderbuffer_read_surfaces,
};

static void
update_stage_texture_surfaces(struct brw_context *brw,
                              const struct gl_program *prog,
                              struct brw_stage_state *stage_state,
                              bool for_gather, uint32_t plane)
{
   if (!prog)
      return;

   struct gl_context *ctx = &brw->ctx;

   uint32_t *surf_offset = stage_state->surf_offset;

   /* BRW_NEW_*_PROG_DATA */
   if (for_gather)
      surf_offset += stage_state->prog_data->binding_table.gather_texture_start;
   else
      surf_offset += stage_state->prog_data->binding_table.plane_start[plane];

   unsigned num_samplers = util_last_bit(prog->SamplersUsed);
   for (unsigned s = 0; s < num_samplers; s++) {
      surf_offset[s] = 0;

      if (prog->SamplersUsed & (1 << s)) {
         const unsigned unit = prog->SamplerUnits[s];

         /* _NEW_TEXTURE */
         if (ctx->Texture.Unit[unit]._Current) {
            brw_update_texture_surface(ctx, unit, surf_offset + s, for_gather, plane);
         }
      }
   }
}


/**
 * Construct SURFACE_STATE objects for enabled textures.
 */
static void
brw_update_texture_surfaces(struct brw_context *brw)
{
   /* BRW_NEW_VERTEX_PROGRAM */
   struct gl_program *vs = (struct gl_program *) brw->vertex_program;

   /* BRW_NEW_TESS_PROGRAMS */
   struct gl_program *tcs = (struct gl_program *) brw->tess_ctrl_program;
   struct gl_program *tes = (struct gl_program *) brw->tess_eval_program;

   /* BRW_NEW_GEOMETRY_PROGRAM */
   struct gl_program *gs = (struct gl_program *) brw->geometry_program;

   /* BRW_NEW_FRAGMENT_PROGRAM */
   struct gl_program *fs = (struct gl_program *) brw->fragment_program;

   /* _NEW_TEXTURE */
   update_stage_texture_surfaces(brw, vs, &brw->vs.base, false, 0);
   update_stage_texture_surfaces(brw, tcs, &brw->tcs.base, false, 0);
   update_stage_texture_surfaces(brw, tes, &brw->tes.base, false, 0);
   update_stage_texture_surfaces(brw, gs, &brw->gs.base, false, 0);
   update_stage_texture_surfaces(brw, fs, &brw->wm.base, false, 0);

   /* emit alternate set of surface state for gather. this
    * allows the surface format to be overriden for only the
    * gather4 messages. */
   if (brw->gen < 8) {
      if (vs && vs->nir->info->uses_texture_gather)
         update_stage_texture_surfaces(brw, vs, &brw->vs.base, true, 0);
      if (tcs && tcs->nir->info->uses_texture_gather)
         update_stage_texture_surfaces(brw, tcs, &brw->tcs.base, true, 0);
      if (tes && tes->nir->info->uses_texture_gather)
         update_stage_texture_surfaces(brw, tes, &brw->tes.base, true, 0);
      if (gs && gs->nir->info->uses_texture_gather)
         update_stage_texture_surfaces(brw, gs, &brw->gs.base, true, 0);
      if (fs && fs->nir->info->uses_texture_gather)
         update_stage_texture_surfaces(brw, fs, &brw->wm.base, true, 0);
   }

   if (fs) {
      update_stage_texture_surfaces(brw, fs, &brw->wm.base, false, 1);
      update_stage_texture_surfaces(brw, fs, &brw->wm.base, false, 2);
   }

   brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
}

const struct brw_tracked_state brw_texture_surfaces = {
   .dirty = {
      .mesa = _NEW_TEXTURE,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_FRAGMENT_PROGRAM |
             BRW_NEW_FS_PROG_DATA |
             BRW_NEW_GEOMETRY_PROGRAM |
             BRW_NEW_GS_PROG_DATA |
             BRW_NEW_TESS_PROGRAMS |
             BRW_NEW_TCS_PROG_DATA |
             BRW_NEW_TES_PROG_DATA |
             BRW_NEW_TEXTURE_BUFFER |
             BRW_NEW_VERTEX_PROGRAM |
             BRW_NEW_VS_PROG_DATA,
   },
   .emit = brw_update_texture_surfaces,
};

static void
brw_update_cs_texture_surfaces(struct brw_context *brw)
{
   /* BRW_NEW_COMPUTE_PROGRAM */
   struct gl_program *cs = (struct gl_program *) brw->compute_program;

   /* _NEW_TEXTURE */
   update_stage_texture_surfaces(brw, cs, &brw->cs.base, false, 0);

   /* emit alternate set of surface state for gather. this
    * allows the surface format to be overriden for only the
    * gather4 messages.
    */
   if (brw->gen < 8) {
      if (cs && cs->nir->info->uses_texture_gather)
         update_stage_texture_surfaces(brw, cs, &brw->cs.base, true, 0);
   }

   brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
}

const struct brw_tracked_state brw_cs_texture_surfaces = {
   .dirty = {
      .mesa = _NEW_TEXTURE,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_COMPUTE_PROGRAM,
   },
   .emit = brw_update_cs_texture_surfaces,
};


void
brw_upload_ubo_surfaces(struct brw_context *brw, struct gl_program *prog,
                        struct brw_stage_state *stage_state,
                        struct brw_stage_prog_data *prog_data)
{
   struct gl_context *ctx = &brw->ctx;

   if (!prog)
      return;

   uint32_t *ubo_surf_offsets =
      &stage_state->surf_offset[prog_data->binding_table.ubo_start];

   for (int i = 0; i < prog->info.num_ubos; i++) {
      struct gl_uniform_buffer_binding *binding =
         &ctx->UniformBufferBindings[prog->sh.UniformBlocks[i]->Binding];

      if (binding->BufferObject == ctx->Shared->NullBufferObj) {
         brw->vtbl.emit_null_surface_state(brw, 1, 1, 1, &ubo_surf_offsets[i]);
      } else {
         struct intel_buffer_object *intel_bo =
            intel_buffer_object(binding->BufferObject);
         GLsizeiptr size = binding->BufferObject->Size - binding->Offset;
         if (!binding->AutomaticSize)
            size = MIN2(size, binding->Size);
         drm_intel_bo *bo =
            intel_bufferobj_buffer(brw, intel_bo,
                                   binding->Offset,
                                   size);
         brw_create_constant_surface(brw, bo, binding->Offset,
                                     size,
                                     &ubo_surf_offsets[i]);
      }
   }

   uint32_t *ssbo_surf_offsets =
      &stage_state->surf_offset[prog_data->binding_table.ssbo_start];

   for (int i = 0; i < prog->info.num_ssbos; i++) {
      struct gl_shader_storage_buffer_binding *binding =
         &ctx->ShaderStorageBufferBindings[prog->sh.ShaderStorageBlocks[i]->Binding];

      if (binding->BufferObject == ctx->Shared->NullBufferObj) {
         brw->vtbl.emit_null_surface_state(brw, 1, 1, 1, &ssbo_surf_offsets[i]);
      } else {
         struct intel_buffer_object *intel_bo =
            intel_buffer_object(binding->BufferObject);
         GLsizeiptr size = binding->BufferObject->Size - binding->Offset;
         if (!binding->AutomaticSize)
            size = MIN2(size, binding->Size);
         drm_intel_bo *bo =
            intel_bufferobj_buffer(brw, intel_bo,
                                   binding->Offset,
                                   size);
         brw_create_buffer_surface(brw, bo, binding->Offset,
                                   size,
                                   &ssbo_surf_offsets[i]);
      }
   }

   if (prog->info.num_ubos || prog->info.num_ssbos)
      brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
}

static void
brw_upload_wm_ubo_surfaces(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* _NEW_PROGRAM */
   struct gl_program *prog = ctx->_Shader->_CurrentFragmentProgram;

   /* BRW_NEW_FS_PROG_DATA */
   brw_upload_ubo_surfaces(brw, prog, &brw->wm.base, brw->wm.base.prog_data);
}

const struct brw_tracked_state brw_wm_ubo_surfaces = {
   .dirty = {
      .mesa = _NEW_PROGRAM,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_FS_PROG_DATA |
             BRW_NEW_UNIFORM_BUFFER,
   },
   .emit = brw_upload_wm_ubo_surfaces,
};

static void
brw_upload_cs_ubo_surfaces(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* _NEW_PROGRAM */
   struct gl_shader_program *prog =
      ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];

   if (!prog || !prog->_LinkedShaders[MESA_SHADER_COMPUTE])
      return;

   /* BRW_NEW_CS_PROG_DATA */
   brw_upload_ubo_surfaces(brw, prog->_LinkedShaders[MESA_SHADER_COMPUTE]->Program,
                           &brw->cs.base, brw->cs.base.prog_data);
}

const struct brw_tracked_state brw_cs_ubo_surfaces = {
   .dirty = {
      .mesa = _NEW_PROGRAM,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_CS_PROG_DATA |
             BRW_NEW_UNIFORM_BUFFER,
   },
   .emit = brw_upload_cs_ubo_surfaces,
};

void
brw_upload_abo_surfaces(struct brw_context *brw,
                        const struct gl_program *prog,
                        struct brw_stage_state *stage_state,
                        struct brw_stage_prog_data *prog_data)
{
   struct gl_context *ctx = &brw->ctx;
   uint32_t *surf_offsets =
      &stage_state->surf_offset[prog_data->binding_table.abo_start];

   if (prog->info.num_abos) {
      for (unsigned i = 0; i < prog->info.num_abos; i++) {
         struct gl_atomic_buffer_binding *binding =
            &ctx->AtomicBufferBindings[prog->sh.AtomicBuffers[i]->Binding];
         struct intel_buffer_object *intel_bo =
            intel_buffer_object(binding->BufferObject);
         drm_intel_bo *bo = intel_bufferobj_buffer(
            brw, intel_bo, binding->Offset, intel_bo->Base.Size - binding->Offset);

         brw_emit_buffer_surface_state(brw, &surf_offsets[i], bo,
                                       binding->Offset, BRW_SURFACEFORMAT_RAW,
                                       bo->size - binding->Offset, 1, true);
      }

      brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
   }
}

static void
brw_upload_wm_abo_surfaces(struct brw_context *brw)
{
   /* _NEW_PROGRAM */
   const struct gl_program *wm = brw->fragment_program;

   if (wm) {
      /* BRW_NEW_FS_PROG_DATA */
      brw_upload_abo_surfaces(brw, wm, &brw->wm.base, brw->wm.base.prog_data);
   }
}

const struct brw_tracked_state brw_wm_abo_surfaces = {
   .dirty = {
      .mesa = _NEW_PROGRAM,
      .brw = BRW_NEW_ATOMIC_BUFFER |
             BRW_NEW_BLORP |
             BRW_NEW_BATCH |
             BRW_NEW_FS_PROG_DATA,
   },
   .emit = brw_upload_wm_abo_surfaces,
};

static void
brw_upload_cs_abo_surfaces(struct brw_context *brw)
{
   /* _NEW_PROGRAM */
   const struct gl_program *cp = brw->compute_program;

   if (cp) {
      /* BRW_NEW_CS_PROG_DATA */
      brw_upload_abo_surfaces(brw, cp, &brw->cs.base, brw->cs.base.prog_data);
   }
}

const struct brw_tracked_state brw_cs_abo_surfaces = {
   .dirty = {
      .mesa = _NEW_PROGRAM,
      .brw = BRW_NEW_ATOMIC_BUFFER |
             BRW_NEW_BLORP |
             BRW_NEW_BATCH |
             BRW_NEW_CS_PROG_DATA,
   },
   .emit = brw_upload_cs_abo_surfaces,
};

static void
brw_upload_cs_image_surfaces(struct brw_context *brw)
{
   /* _NEW_PROGRAM */
   const struct gl_program *cp = brw->compute_program;

   if (cp) {
      /* BRW_NEW_CS_PROG_DATA, BRW_NEW_IMAGE_UNITS, _NEW_TEXTURE */
      brw_upload_image_surfaces(brw, cp, &brw->cs.base,
                                brw->cs.base.prog_data);
   }
}

const struct brw_tracked_state brw_cs_image_surfaces = {
   .dirty = {
      .mesa = _NEW_TEXTURE | _NEW_PROGRAM,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_CS_PROG_DATA |
             BRW_NEW_IMAGE_UNITS
   },
   .emit = brw_upload_cs_image_surfaces,
};

static uint32_t
get_image_format(struct brw_context *brw, mesa_format format, GLenum access)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   uint32_t hw_format = brw_format_for_mesa_format(format);
   if (access == GL_WRITE_ONLY) {
      return hw_format;
   } else if (isl_has_matching_typed_storage_image_format(devinfo, hw_format)) {
      /* Typed surface reads support a very limited subset of the shader
       * image formats.  Translate it into the closest format the
       * hardware supports.
       */
      return isl_lower_storage_image_format(devinfo, hw_format);
   } else {
      /* The hardware doesn't actually support a typed format that we can use
       * so we have to fall back to untyped read/write messages.
       */
      return BRW_SURFACEFORMAT_RAW;
   }
}

static void
update_default_image_param(struct brw_context *brw,
                           struct gl_image_unit *u,
                           unsigned surface_idx,
                           struct brw_image_param *param)
{
   memset(param, 0, sizeof(*param));
   param->surface_idx = surface_idx;
   /* Set the swizzling shifts to all-ones to effectively disable swizzling --
    * See emit_address_calculation() in brw_fs_surface_builder.cpp for a more
    * detailed explanation of these parameters.
    */
   param->swizzling[0] = 0xff;
   param->swizzling[1] = 0xff;
}

static void
update_buffer_image_param(struct brw_context *brw,
                          struct gl_image_unit *u,
                          unsigned surface_idx,
                          struct brw_image_param *param)
{
   struct gl_buffer_object *obj = u->TexObj->BufferObject;
   const uint32_t size = MIN2((uint32_t)u->TexObj->BufferSize, obj->Size);
   update_default_image_param(brw, u, surface_idx, param);

   param->size[0] = size / _mesa_get_format_bytes(u->_ActualFormat);
   param->stride[0] = _mesa_get_format_bytes(u->_ActualFormat);
}

static void
update_texture_image_param(struct brw_context *brw,
                           struct gl_image_unit *u,
                           unsigned surface_idx,
                           struct brw_image_param *param)
{
   struct intel_mipmap_tree *mt = intel_texture_object(u->TexObj)->mt;

   update_default_image_param(brw, u, surface_idx, param);

   param->size[0] = minify(mt->logical_width0, u->Level);
   param->size[1] = minify(mt->logical_height0, u->Level);
   param->size[2] = (!u->Layered ? 1 :
                     u->TexObj->Target == GL_TEXTURE_CUBE_MAP ? 6 :
                     u->TexObj->Target == GL_TEXTURE_3D ?
                     minify(mt->logical_depth0, u->Level) :
                     mt->logical_depth0);

   intel_miptree_get_image_offset(mt, u->Level, u->_Layer,
                                  &param->offset[0],
                                  &param->offset[1]);

   param->stride[0] = mt->cpp;
   param->stride[1] = mt->pitch / mt->cpp;
   param->stride[2] =
      brw_miptree_get_horizontal_slice_pitch(brw, mt, u->Level);
   param->stride[3] =
      brw_miptree_get_vertical_slice_pitch(brw, mt, u->Level);

   if (mt->tiling == I915_TILING_X) {
      /* An X tile is a rectangular block of 512x8 bytes. */
      param->tiling[0] = _mesa_logbase2(512 / mt->cpp);
      param->tiling[1] = _mesa_logbase2(8);

      if (brw->has_swizzling) {
         /* Right shifts required to swizzle bits 9 and 10 of the memory
          * address with bit 6.
          */
         param->swizzling[0] = 3;
         param->swizzling[1] = 4;
      }
   } else if (mt->tiling == I915_TILING_Y) {
      /* The layout of a Y-tiled surface in memory isn't really fundamentally
       * different to the layout of an X-tiled surface, we simply pretend that
       * the surface is broken up in a number of smaller 16Bx32 tiles, each
       * one arranged in X-major order just like is the case for X-tiling.
       */
      param->tiling[0] = _mesa_logbase2(16 / mt->cpp);
      param->tiling[1] = _mesa_logbase2(32);

      if (brw->has_swizzling) {
         /* Right shift required to swizzle bit 9 of the memory address with
          * bit 6.
          */
         param->swizzling[0] = 3;
      }
   }

   /* 3D textures are arranged in 2D in memory with 2^lod slices per row.  The
    * address calculation algorithm (emit_address_calculation() in
    * brw_fs_surface_builder.cpp) handles this as a sort of tiling with
    * modulus equal to the LOD.
    */
   param->tiling[2] = (u->TexObj->Target == GL_TEXTURE_3D ? u->Level :
                       0);
}

static void
update_image_surface(struct brw_context *brw,
                     struct gl_image_unit *u,
                     GLenum access,
                     unsigned surface_idx,
                     uint32_t *surf_offset,
                     struct brw_image_param *param)
{
   if (_mesa_is_image_unit_valid(&brw->ctx, u)) {
      struct gl_texture_object *obj = u->TexObj;
      const unsigned format = get_image_format(brw, u->_ActualFormat, access);

      if (obj->Target == GL_TEXTURE_BUFFER) {
         struct intel_buffer_object *intel_obj =
            intel_buffer_object(obj->BufferObject);
         const unsigned texel_size = (format == BRW_SURFACEFORMAT_RAW ? 1 :
                                      _mesa_get_format_bytes(u->_ActualFormat));

         brw_emit_buffer_surface_state(
            brw, surf_offset, intel_obj->buffer, obj->BufferOffset,
            format, intel_obj->Base.Size, texel_size,
            access != GL_READ_ONLY);

         update_buffer_image_param(brw, u, surface_idx, param);

      } else {
         struct intel_texture_object *intel_obj = intel_texture_object(obj);
         struct intel_mipmap_tree *mt = intel_obj->mt;

         if (format == BRW_SURFACEFORMAT_RAW) {
            brw_emit_buffer_surface_state(
               brw, surf_offset, mt->bo, mt->offset,
               format, mt->bo->size - mt->offset, 1 /* pitch */,
               access != GL_READ_ONLY);

         } else {
            const unsigned num_layers = (!u->Layered ? 1 :
                                         obj->Target == GL_TEXTURE_CUBE_MAP ? 6 :
                                         mt->logical_depth0);

            struct isl_view view = {
               .format = format,
               .base_level = obj->MinLevel + u->Level,
               .levels = 1,
               .base_array_layer = obj->MinLayer + u->_Layer,
               .array_len = num_layers,
               .swizzle = ISL_SWIZZLE_IDENTITY,
               .usage = ISL_SURF_USAGE_STORAGE_BIT,
            };

            const int surf_index = surf_offset - &brw->wm.base.surf_offset[0];
            const bool unresolved = intel_miptree_has_color_unresolved(
                                       mt, view.base_level, view.levels,
                                       view.base_array_layer, view.array_len);
            const int flags = unresolved ? 0 : INTEL_AUX_BUFFER_DISABLED;
            brw_emit_surface_state(brw, mt, flags, mt->target, view,
                                   tex_mocs[brw->gen],
                                   surf_offset, surf_index,
                                   I915_GEM_DOMAIN_SAMPLER,
                                   access == GL_READ_ONLY ? 0 :
                                             I915_GEM_DOMAIN_SAMPLER);
         }

         update_texture_image_param(brw, u, surface_idx, param);
      }

   } else {
      brw->vtbl.emit_null_surface_state(brw, 1, 1, 1, surf_offset);
      update_default_image_param(brw, u, surface_idx, param);
   }
}

void
brw_upload_image_surfaces(struct brw_context *brw,
                          const struct gl_program *prog,
                          struct brw_stage_state *stage_state,
                          struct brw_stage_prog_data *prog_data)
{
   assert(prog);
   struct gl_context *ctx = &brw->ctx;

   if (prog->info.num_images) {
      for (unsigned i = 0; i < prog->info.num_images; i++) {
         struct gl_image_unit *u = &ctx->ImageUnits[prog->sh.ImageUnits[i]];
         const unsigned surf_idx = prog_data->binding_table.image_start + i;

         update_image_surface(brw, u, prog->sh.ImageAccess[i],
                              surf_idx,
                              &stage_state->surf_offset[surf_idx],
                              &prog_data->image_param[i]);
      }

      brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
      /* This may have changed the image metadata dependent on the context
       * image unit state and passed to the program as uniforms, make sure
       * that push and pull constants are reuploaded.
       */
      brw->NewGLState |= _NEW_PROGRAM_CONSTANTS;
   }
}

static void
brw_upload_wm_image_surfaces(struct brw_context *brw)
{
   /* BRW_NEW_FRAGMENT_PROGRAM */
   const struct gl_program *wm = brw->fragment_program;

   if (wm) {
      /* BRW_NEW_FS_PROG_DATA, BRW_NEW_IMAGE_UNITS, _NEW_TEXTURE */
      brw_upload_image_surfaces(brw, wm, &brw->wm.base,
                                brw->wm.base.prog_data);
   }
}

const struct brw_tracked_state brw_wm_image_surfaces = {
   .dirty = {
      .mesa = _NEW_TEXTURE,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_FRAGMENT_PROGRAM |
             BRW_NEW_FS_PROG_DATA |
             BRW_NEW_IMAGE_UNITS
   },
   .emit = brw_upload_wm_image_surfaces,
};

void
gen4_init_vtable_surface_functions(struct brw_context *brw)
{
   brw->vtbl.update_renderbuffer_surface = gen4_update_renderbuffer_surface;
   brw->vtbl.emit_null_surface_state = brw_emit_null_surface_state;
}

void
gen6_init_vtable_surface_functions(struct brw_context *brw)
{
   gen4_init_vtable_surface_functions(brw);
   brw->vtbl.update_renderbuffer_surface = brw_update_renderbuffer_surface;
}

static void
brw_upload_cs_work_groups_surface(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* _NEW_PROGRAM */
   struct gl_shader_program *prog =
      ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];
   /* BRW_NEW_CS_PROG_DATA */
   const struct brw_cs_prog_data *cs_prog_data =
      brw_cs_prog_data(brw->cs.base.prog_data);

   if (prog && cs_prog_data->uses_num_work_groups) {
      const unsigned surf_idx =
         cs_prog_data->binding_table.work_groups_start;
      uint32_t *surf_offset = &brw->cs.base.surf_offset[surf_idx];
      drm_intel_bo *bo;
      uint32_t bo_offset;

      if (brw->compute.num_work_groups_bo == NULL) {
         bo = NULL;
         intel_upload_data(brw,
                           (void *)brw->compute.num_work_groups,
                           3 * sizeof(GLuint),
                           sizeof(GLuint),
                           &bo,
                           &bo_offset);
      } else {
         bo = brw->compute.num_work_groups_bo;
         bo_offset = brw->compute.num_work_groups_offset;
      }

      brw_emit_buffer_surface_state(brw, surf_offset,
                                    bo, bo_offset,
                                    BRW_SURFACEFORMAT_RAW,
                                    3 * sizeof(GLuint), 1, true);
      brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
   }
}

const struct brw_tracked_state brw_cs_work_groups_surface = {
   .dirty = {
      .brw = BRW_NEW_BLORP |
             BRW_NEW_CS_PROG_DATA |
             BRW_NEW_CS_WORK_GROUPS
   },
   .emit = brw_upload_cs_work_groups_surface,
};
