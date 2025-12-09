/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 * Copyright (C) 2014-2017 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2023 Amazon.com, Inc. or its affiliates
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
 * Authors (Collabora):
 *   Tomeu Vizoso <tomeu.vizoso@collabora.com>
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#include <fcntl.h>
#include <xf86drm.h>
#include "drm-uapi/drm_fourcc.h"

#include "frontend/winsys_handle.h"
#include "util/format/u_format.h"
#include "util/u_debug_image.h"
#include "util/u_drm.h"
#include "util/u_gen_mipmap.h"
#include "util/u_memory.h"
#include "util/u_resource.h"
#include "util/u_surface.h"
#include "util/u_transfer.h"
#include "util/u_transfer_helper.h"
#include "util/perf/cpu_trace.h"
#include "util/streaming-load-memcpy.h"

#include "decode.h"
#include "pan_afbc.h"
#include "pan_afrc.h"
#include "pan_bo.h"
#include "pan_context.h"
#include "pan_resource.h"
#include "pan_screen.h"
#include "pan_tiling.h"
#include "pan_util.h"

static void
panfrost_clear_depth_stencil(struct pipe_context *pipe,
                             struct pipe_surface *dst, unsigned clear_flags,
                             double depth, unsigned stencil, unsigned dstx,
                             unsigned dsty, unsigned width, unsigned height,
                             bool render_condition_enabled)
{
   struct panfrost_context *ctx = pan_context(pipe);

   if (render_condition_enabled && !panfrost_render_condition_check(ctx))
      return;

   /* Legalize here because it could trigger a recursive blit otherwise */
   struct panfrost_resource *rdst = pan_resource(dst->texture);
   enum pipe_format dst_view_format = util_format_linear(dst->format);
   pan_legalize_format(ctx, rdst, dst_view_format, true, false);

   panfrost_blitter_save(
      ctx, render_condition_enabled ? PAN_RENDER_COND : PAN_RENDER_BASE);
   util_blitter_clear_depth_stencil(ctx->blitter, dst, clear_flags, depth,
                                    stencil, dstx, dsty, width, height);
}

static void
panfrost_clear_render_target(struct pipe_context *pipe,
                             struct pipe_surface *dst,
                             const union pipe_color_union *color, unsigned dstx,
                             unsigned dsty, unsigned width, unsigned height,
                             bool render_condition_enabled)
{
   struct panfrost_context *ctx = pan_context(pipe);

   if (render_condition_enabled && !panfrost_render_condition_check(ctx))
      return;

   /* Legalize here because it could trigger a recursive blit otherwise */
   struct panfrost_resource *rdst = pan_resource(dst->texture);
   enum pipe_format dst_view_format = util_format_linear(dst->format);
   pan_legalize_format(ctx, rdst, dst_view_format, true, false);

   panfrost_blitter_save(
      ctx, (render_condition_enabled ? PAN_RENDER_COND : PAN_RENDER_BASE) | PAN_SAVE_FRAGMENT_CONSTANT);
   util_blitter_clear_render_target(ctx->blitter, dst, color, dstx, dsty, width,
                                    height);
}

static bool
panfrost_resource_init_image(
   struct pipe_screen *screen, struct panfrost_resource *rsc,
   const struct pan_image_props *iprops, unsigned plane_idx,
   const struct pan_image_layout_constraints *explicit_layout)
{
   struct panfrost_device *dev = pan_device(screen);
   const unsigned format_plane_count = util_format_get_num_planes(iprops->format);

   /* Some planar formats are lowered by the frontend, assume each plane is
    * independent in that case. */
   if (format_plane_count == 1)
      plane_idx = 0;

   rsc->image = (struct pan_image){
      .mod_handler = pan_mod_get_handler(dev->arch, iprops->modifier),
      .props = *iprops,
   };
   rsc->image.planes[plane_idx] = &rsc->plane;

   if (!pan_image_layout_init(dev->arch, &rsc->image, plane_idx,
                              explicit_layout))
      return false;

   /* The rest of the resource planes will be initialized when we hit the first
    * plane. */
   if (plane_idx > 0 || format_plane_count == 1)
      return true;

   plane_idx = 1;
   for (struct panfrost_resource *plane = pan_resource(rsc->base.next);
        plane && plane_idx < ARRAY_SIZE(rsc->image.planes);
        plane = pan_resource(plane->base.next))
      rsc->image.planes[plane_idx++] = &plane->plane;

   assert(plane_idx == util_format_get_num_planes(iprops->format));

   for (struct panfrost_resource *plane = pan_resource(rsc->base.next);
        plane; plane = pan_resource(plane->base.next)) {
      memcpy(plane->image.planes, rsc->image.planes, sizeof(plane->image.planes));
   }

   return true;
}

static bool
adjust_mtk_tiled_props(struct panfrost_resource *rsc,
                       struct pan_image_props *iprops, unsigned plane_idx,
                       struct pan_image_layout_constraints *explicit_layout)
{
   bool is_uv_plane =
      iprops->format == PIPE_FORMAT_R8G8_UNORM ||
      (iprops->format == PIPE_FORMAT_R8_G8B8_420_UNORM && plane_idx > 0);
   unsigned tile_w_px, tile_h_px, blksz_B;

   if (is_uv_plane) {
      tile_w_px = 8;
      tile_h_px = 16;
      blksz_B = 2;
      iprops->format = PIPE_FORMAT_R8G8_UNORM;
   } else {
      tile_w_px = 16;
      tile_h_px = 32;
      blksz_B = 1;
      iprops->format = PIPE_FORMAT_R8_UNORM;
   }

   /* SW detiling on MTK_TILED resources. This forces us to treat such
    * resources as linear images with:
    *    width = tile_width * tile_height
    *    height = (wsi_row_stride / (tile_width * blksize)) * (height /
    * tile_height)
    */
   iprops->extent_px.width = tile_w_px * tile_h_px;
   iprops->extent_px.height =
      (explicit_layout->wsi_row_pitch_B / (blksz_B * tile_w_px)) *
      DIV_ROUND_UP(rsc->base.height0, tile_h_px);

   /* Reject the import if the pitch is not aligned on a tile or if it's not
    * covering the resource width. */
   unsigned min_row_pitch_B = rsc->base.width0 * blksz_B;
   unsigned row_pitch_align_req_B = blksz_B * tile_w_px;

   if (explicit_layout->strict &&
       (explicit_layout->wsi_row_pitch_B % row_pitch_align_req_B != 0 ||
        explicit_layout->wsi_row_pitch_B < min_row_pitch_B))
      return false;

   /* Now adjust the row pitch. */
   explicit_layout->wsi_row_pitch_B = iprops->extent_px.width * blksz_B;
   return true;
}

static void
pan_resource_afbcp_restart(struct panfrost_resource *prsrc)
{
   assert(prsrc->base.array_size == 1);

   if (!prsrc->afbcp) {
      prsrc->afbcp = calloc(1, sizeof(struct pan_afbcp));
      return;
   }

   if (prsrc->afbcp->layout_bo) {
      panfrost_bo_unreference(prsrc->afbcp->layout_bo);
      prsrc->afbcp->layout_bo = NULL;
   }
   if (prsrc->afbcp->packed_bo) {
      panfrost_bo_unreference(prsrc->afbcp->packed_bo);
      prsrc->afbcp->packed_bo = NULL;
   }

   prsrc->afbcp->size = 0;
   prsrc->afbcp->ratio = 0.0f;
   prsrc->afbcp->nr_consecutive_reads = 0;
}

static void
pan_resource_afbcp_stop(struct panfrost_resource *prsrc)
{
   if (!prsrc->afbcp)
      return;

   if (prsrc->afbcp->layout_bo)
      panfrost_bo_unreference(prsrc->afbcp->layout_bo);
   if (prsrc->afbcp->packed_bo)
      panfrost_bo_unreference(prsrc->afbcp->packed_bo);

   free(prsrc->afbcp);
   prsrc->afbcp = NULL;
}

static void
panfrost_resource_destroy(struct pipe_screen *screen, struct pipe_resource *pt)
{
   MESA_TRACE_FUNC();

   struct panfrost_device *dev = pan_device(screen);
   struct panfrost_resource *rsrc = (struct panfrost_resource *)pt;

   if (rsrc->scanout)
      renderonly_scanout_destroy(rsrc->scanout, dev->ro);

   if (rsrc->shadow_image)
      pipe_resource_reference(
         (struct pipe_resource **)&rsrc->shadow_image, NULL);

   if (rsrc->bo) {
      if (rsrc->owns_label) {
         /* The resource owns the label, which it dynamically allocated, so
          * it is safe to discard the const qualifier */
         char *rsrc_label = (char *)panfrost_bo_replace_label(rsrc->bo, "Destroyed resource", false);
         free(rsrc_label);
      }
      panfrost_bo_unreference(rsrc->bo);
   }

   pan_resource_afbcp_stop(rsrc);

   free(rsrc->index_cache);
   free(rsrc->damage.tile_map.data);

   util_range_destroy(&rsrc->valid_buffer_range);
   free(rsrc);
}

static int
panfrost_resource_import_bo(struct panfrost_resource *rsc,
                            struct panfrost_device *dev, int fd)
{
   if (!rsc)
      return -1;

   rsc->owns_label = false;

   rsc->bo = panfrost_bo_import(dev, fd);
   if (!rsc->bo)
      return -1;

   return 0;
}

static const char *
panfrost_resource_type_str(struct panfrost_resource *rsrc)
{
   /* Guess a label based on the bind */
   unsigned bind = rsrc->base.bind;
   const char *type = (bind & PIPE_BIND_INDEX_BUFFER)     ? "Index buffer"
                      : (bind & PIPE_BIND_SCANOUT)        ? "Scanout"
                      : (bind & PIPE_BIND_DISPLAY_TARGET) ? "Display target"
                      : (bind & PIPE_BIND_SHARED)         ? "Shared resource"
                      : (bind & PIPE_BIND_RENDER_TARGET)  ? "Render target"
                      : (bind & PIPE_BIND_DEPTH_STENCIL)
                         ? "Depth/stencil buffer"
                      : (bind & PIPE_BIND_SAMPLER_VIEW)    ? "Texture"
                      : (bind & PIPE_BIND_VERTEX_BUFFER)   ? "Vertex buffer"
                      : (bind & PIPE_BIND_CONSTANT_BUFFER) ? "Constant buffer"
                      : (bind & PIPE_BIND_GLOBAL)          ? "Global memory"
                      : (bind & PIPE_BIND_SHADER_BUFFER)   ? "Shader buffer"
                      : (bind & PIPE_BIND_SHADER_IMAGE)    ? "Shader image"
                                                           : "Other resource";
   return type;
}

static char *
panfrost_resource_new_label(struct panfrost_resource *rsrc,
                            uint64_t modifier,
                            const char *user_label)
{
   char *new_label = NULL;

   asprintf(&new_label,
            "%s format=%s extent=%ux%ux%u array_size=%u mip_count=%u samples=%u modifier=0x%"PRIx64"%s%s",
            panfrost_resource_type_str(rsrc),
            util_format_short_name(rsrc->base.format),
            rsrc->base.width0, rsrc->base.height0, rsrc->base.depth0,
            rsrc->base.array_size, rsrc->base.last_level,
            rsrc->base.nr_storage_samples, modifier,
            user_label ? " user_label=" : "",
            user_label ? : "");

   return new_label;
}

static bool
strict_import(struct panfrost_device *dev, uint64_t mod,
              enum pipe_format format)
{
   if (dev->debug & PAN_DBG_STRICT_IMPORT)
      return true;

   /* AFBC(YUV) has been introduced after the stricter import rules, let's
    * make them strict by default. */
   if (drm_is_afbc(mod) && pan_format_is_yuv(format) && !dev->relaxed_afbc_yuv_imports)
      return true;

   /* Linear and u-tiled imports have always been strict. The only ones that
    * were lax are AFBC, AFRC and MTK_TILED. Make sure we encourage new
    * modifiers to enforce strict rules by default. */
   return !(drm_is_afbc(mod) || drm_is_afrc(mod) || drm_is_mtk_tiled(mod));
}

static struct pipe_resource *
panfrost_resource_from_handle(struct pipe_screen *pscreen,
                              const struct pipe_resource *templat,
                              struct winsys_handle *whandle, unsigned usage)
{
   struct panfrost_device *dev = pan_device(pscreen);
   struct panfrost_resource *rsc;
   struct pipe_resource *prsc;

   assert(whandle->type == WINSYS_HANDLE_TYPE_FD);

   rsc = CALLOC_STRUCT(panfrost_resource);
   if (!rsc)
      return NULL;

   prsc = &rsc->base;

   *prsc = *templat;

   pipe_reference_init(&prsc->reference, 1);
   util_range_init(&rsc->valid_buffer_range);
   prsc->screen = pscreen;

   uint64_t mod = whandle->modifier == DRM_FORMAT_MOD_INVALID
                     ? DRM_FORMAT_MOD_LINEAR
                     : whandle->modifier;
   enum mali_texture_dimension dim =
      panfrost_translate_texture_dimension(templat->target);
   struct pan_image_layout_constraints explicit_layout = {
      .offset_B = whandle->offset,
      .wsi_row_pitch_B = whandle->stride,
      .strict = strict_import(dev, mod, templat->format),
   };

   rsc->modifier = mod;

   bool h_subsamp =
      util_format_get_plane_width(templat->format, whandle->plane, 2) == 1;
   bool v_subsamp =
      util_format_get_plane_height(templat->format, whandle->plane, 2) == 1;
   struct pan_image_props iprops = {
      /* pan_layout doesn't know about MTK modifiers, so make it
       * linear before calling pan_image_layout_init(). */
      .modifier = panfrost_is_emulated_mod(mod) ? DRM_FORMAT_MOD_LINEAR : mod,
      .format = templat->format,
      .dim = dim,
      .extent_px = {
         /* pan_layout_init() wants the property of the full image, not the
          * plane, but pipe_resource encodes the properties of the plane.
          * We need to adjust the width/height according to the subsampling
          * properties. */
         .width = prsc->width0 * (h_subsamp ? 2 : 1),
         .height = prsc->height0 * (v_subsamp ? 2 : 1),
         .depth = prsc->depth0,
      },
      .array_size = prsc->array_size,
      .nr_samples = MAX2(prsc->nr_samples, 1),
      .nr_slices = 1,
   };

   if (drm_is_mtk_tiled(mod) &&
       !adjust_mtk_tiled_props(rsc, &iprops, whandle->plane,
                               &explicit_layout)) {
      panfrost_resource_destroy(pscreen, &rsc->base);
      return NULL;
   }

   bool valid = panfrost_resource_init_image(pscreen, rsc, &iprops,
                                             whandle->plane, &explicit_layout);
   if (!valid) {
      panfrost_resource_destroy(pscreen, &rsc->base);
      return NULL;
   }

   int ret = panfrost_resource_import_bo(rsc, dev, whandle->handle);
   /* Sometimes an import can fail e.g. on an invalid buffer fd, out of
    * memory space to mmap it etc.
    */
   if (ret) {
      panfrost_resource_destroy(pscreen, &rsc->base);
      return NULL;
   }

   rsc->plane.base = rsc->bo->ptr.gpu;
   rsc->modifier_constant = true;

   BITSET_SET(rsc->valid.data, 0);
   panfrost_resource_set_damage_region(pscreen, &rsc->base, 0, NULL);

   if (dev->ro) {
      rsc->scanout =
         renderonly_create_gpu_import_for_resource(prsc, dev->ro, NULL);
      /* failure is expected in some cases.. */
   }

   return prsc;
}

static void
panfrost_set_resource_label(UNUSED struct pipe_screen *pscreen,
                            struct pipe_resource *presource,
                            const char *user_label)

{
   struct panfrost_resource *rsrc = pan_resource(presource);
   const char *new_label;

   /* We don't manage labels for resources with imported BOs */
   if (!rsrc->owns_label)
      return;

   new_label = panfrost_resource_new_label(rsrc,
                                           rsrc->image.props.modifier,
                                           user_label);
   if (!new_label)
      return;

   char *old_label = (char *)panfrost_bo_set_label(rsrc->bo, new_label);
   free(old_label);
}

static bool
panfrost_resource_get_handle(struct pipe_screen *pscreen,
                             struct pipe_context *ctx, struct pipe_resource *pt,
                             struct winsys_handle *handle, unsigned usage)
{
   struct panfrost_device *dev = pan_device(pscreen);
   struct panfrost_resource *rsrc = pan_resource(pt);
   struct renderonly_scanout *scanout;

   if (handle->plane >= ARRAY_SIZE(rsrc->image.planes) ||
       !rsrc->image.planes[handle->plane])
      return false;

   scanout = rsrc->scanout;

   handle->modifier = rsrc->modifier;
   rsrc->modifier_constant = true;

   if (handle->type == WINSYS_HANDLE_TYPE_KMS && dev->ro) {
      return renderonly_get_handle(scanout, handle);
   } else if (handle->type == WINSYS_HANDLE_TYPE_KMS) {
      handle->handle = panfrost_bo_handle(rsrc->bo);
   } else if (handle->type == WINSYS_HANDLE_TYPE_FD) {
      int fd = panfrost_bo_export(rsrc->bo);

      if (fd < 0)
         return false;

      handle->handle = fd;
   } else {
      /* Other handle types not supported */
      return false;
   }

   handle->stride = pan_image_get_wsi_row_pitch(&rsrc->image, handle->plane, 0);
   handle->offset = pan_image_get_wsi_offset(&rsrc->image, handle->plane, 0);

   /* SW detiling on MTK_TILED resources. This forces us to treat such
    * resources as linear images with:
    *    width = tile_width * tile_height
    *    height = (wsi_row_stride / (tile_width * blksize)) * (height / tile_height)
    *
    * We need to extract the original WSI row pitch from this.
    */
   if (drm_is_mtk_tiled(rsrc->modifier)) {
      bool subsamp = handle->plane > 0 ||
                     rsrc->image.props.format == PIPE_FORMAT_R8G8_UNORM;
      unsigned blksz_B = subsamp ? 2 : 1;
      unsigned tile_w_px = 16 / (subsamp ? 2 : 1);
      unsigned tile_h_px = 32 / (subsamp ? 2 : 1);
      unsigned row_stride_tl = rsrc->image.props.extent_px.height /
                               DIV_ROUND_UP(rsrc->base.height0, tile_h_px);

      handle->stride = row_stride_tl * tile_w_px * blksz_B;
   }

   return true;
}

static bool
panfrost_resource_get_param(struct pipe_screen *pscreen,
                            struct pipe_context *pctx,
                            struct pipe_resource *prsc, unsigned plane,
                            unsigned layer, unsigned level,
                            enum pipe_resource_param param, unsigned usage,
                            uint64_t *value)
{
   struct panfrost_resource *rsrc =
      pan_resource(util_resource_at_index(prsc, plane));

   /* Lowered multi plane resource gets each plane allocated independently. */
   if (!rsrc->image.planes[plane])
      plane = 0;

   switch (param) {
   case PIPE_RESOURCE_PARAM_STRIDE:
      *value = pan_image_get_wsi_row_pitch(&rsrc->image, plane, level);
      return true;
   case PIPE_RESOURCE_PARAM_OFFSET:
      *value = pan_image_get_wsi_offset(&rsrc->image, plane, level);
      return true;
   case PIPE_RESOURCE_PARAM_MODIFIER:
      *value = rsrc->modifier;
      return true;
   case PIPE_RESOURCE_PARAM_NPLANES:
      *value = util_resource_num(prsc);
      return true;
   default:
      return false;
   }
}

static void
panfrost_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
   /* TODO */
}

static inline bool
panfrost_is_2d(const struct panfrost_resource *pres)
{
   return (pres->base.target == PIPE_TEXTURE_2D) ||
          (pres->base.target == PIPE_TEXTURE_RECT);
}

/* Based on the usage, determine if it makes sense to use u-inteleaved tiling.
 * We only have routines to tile 2D textures of sane bpps. On the hardware
 * level, not all usages are valid for tiling. Finally, if the app is hinting
 * that the contents frequently change, tiling will be a loss.
 *
 * On platforms where it is supported, AFBC is even better. */

static bool
panfrost_should_afbc(struct panfrost_device *dev,
                     const struct panfrost_resource *pres, enum pipe_format fmt)
{
   /* AFBC resources may be rendered to, textured from, or shared across
    * processes, but may not be used as e.g buffers */
   const unsigned valid_binding =
      PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_RENDER_TARGET | PIPE_BIND_BLENDABLE |
      PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT |
      PIPE_BIND_SHARED;

   if (pres->base.bind & ~valid_binding)
      return false;

   /* AFBC support is optional */
   if (!dev->has_afbc)
      return false;

   /* AFBC<-->staging is expensive */
   if (pres->base.usage == PIPE_USAGE_STREAM)
      return false;

   /* If constant (non-data-dependent) format is requested, don't AFBC: */
   if (pres->base.bind & PIPE_BIND_CONST_BW)
      return false;

   /* We can't do AFBC(Z32)+S8 on v7- because the AFBC ZS target layout overlaps
    * the S target layout. Since we don't know at this point if the Z32 buffer
    * will be attached an S8 buffer, we have to reject AFBC(Z32)
    * unconditionally. */
   if (dev->arch <= 7 && fmt == PIPE_FORMAT_Z32_FLOAT)
      return false;

   /* Only a small selection of formats are AFBC'able */
   if (!pan_afbc_supports_format(dev->arch, fmt))
      return false;

   /* According to the GL spec, -2^(b-1) and -2^(b-1)+1 both map to -1 in the
    * SNORM representation. The Mali implementation seems to clamp the value
    * to the [-2^(b-1)+1, 2^(b-1)-1] range, which is problematic in our
    * staging_resource (LINEAR) -> final_resource (AFBC) transfer path, because
    * we need a bit-exact copy (typically fails tests like
    * dEQP-GLES31.functional.copy_image.non_compressed.viewclass_16_bits.rg8_snorm_r16ui.texture2d_to_renderbuffer
    * if we don't).
    * FIXME: We could get away with src/dst view format adjustments
    * (SNORM->UNORM) in our blits, but we'd have to clearly identify which
    * copies/blits we want to act like that and which ones we don't, and this is
    * far from obvious when looking at the code, so let's filter out AFBC(SNORM)
    * for now. */
   if (util_format_is_snorm(fmt))
      return false;

   /* AFBC does not support layered (GLES3 style) multisampling. Use
    * EXT_multisampled_render_to_texture instead */
   if (pres->base.nr_samples > 1)
      return false;

   switch (pres->base.target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      break;

   case PIPE_TEXTURE_3D:
      /* 3D AFBC is only supported on Bifrost v7+. It's supposed to
       * be supported on Midgard but it doesn't seem to work */
      if (dev->arch < 7)
         return false;

      break;

   default:
      return false;
   }

   /* For one tile, AFBC is a loss compared to u-interleaved */
   if (pres->base.width0 <= 16 && pres->base.height0 <= 16)
      return false;

   /* AFBC headers point to their tile with a 32-bit offset, so we can't
    * have a body size that's bigger than UINT32_MAX. */
   uint64_t body_size = (uint64_t)pres->base.width0 * pres->base.height0 *
                        pres->base.depth0 *
                        util_format_get_blocksize(pres->base.format);
   if (body_size > UINT32_MAX)
      return false;

   /* Otherwise, we'd prefer AFBC as it is dramatically more efficient
    * than linear or usually even u-interleaved */
   return true;
}

/*
 * For a resource we want to use AFBC with, should we use AFBC with tiled
 * headers? On GPUs that support it, this is believed to be beneficial for
 * images that are at least 128x128.
 */
static bool
panfrost_should_tile_afbc(const struct panfrost_screen *screen,
                          const struct panfrost_resource *pres)
{
   return screen->afbc_tiled && pan_afbc_can_tile(screen->dev.arch) &&
          pres->base.width0 >= 128 && pres->base.height0 >= 128;
}

bool
panfrost_should_pack_afbc(struct panfrost_device *dev,
                          const struct panfrost_resource *prsrc)
{
   const unsigned valid_binding = PIPE_BIND_DEPTH_STENCIL |
                                  PIPE_BIND_RENDER_TARGET |
                                  PIPE_BIND_SAMPLER_VIEW;

   BITSET_WORD mask = BITSET_MASK(prsrc->base.last_level + 1);
   const bool mipmap_chain_valid = BITSET_EQUAL(prsrc->valid.data, &mask);

   return pan_afbc_can_pack(prsrc->base.format) && panfrost_is_2d(prsrc) &&
          drm_is_afbc(prsrc->modifier) &&
          (prsrc->modifier & AFBC_FORMAT_MOD_SPARSE) &&
          !(prsrc->modifier & AFBC_FORMAT_MOD_SPLIT) &&
          (prsrc->base.bind & ~valid_binding) == 0 &&
          !prsrc->modifier_constant && prsrc->base.array_size == 1 &&
          prsrc->base.width0 >= 32 && prsrc->base.height0 >= 32 &&
          mipmap_chain_valid;
}

static bool
panfrost_should_tile(struct panfrost_device *dev,
                     const struct panfrost_resource *pres, enum pipe_format fmt)
{
   const unsigned valid_binding =
      PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_RENDER_TARGET | PIPE_BIND_BLENDABLE |
      PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT |
      PIPE_BIND_SHARED;

   /* The purpose of tiling is improving locality in both X- and
    * Y-directions. If there is only a single pixel in either direction,
    * tiling does not make sense; using a linear layout instead is optimal
    * for both memory usage and performance.
    */
   if (MIN2(pres->base.width0, pres->base.height0) < 2)
      return false;

   bool can_tile = (pres->base.target != PIPE_BUFFER) &&
                   ((pres->base.bind & ~valid_binding) == 0);

   return can_tile && (pres->base.usage != PIPE_USAGE_STREAM);
}

static bool
panfrost_should_afrc(struct panfrost_device *dev,
                     const struct panfrost_resource *pres, enum pipe_format fmt)
{
   const unsigned valid_binding = PIPE_BIND_RENDER_TARGET |
                                  PIPE_BIND_BLENDABLE | PIPE_BIND_SAMPLER_VIEW |
                                  PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SHARED;

   if (pres->base.bind & ~valid_binding)
      return false;

   /* AFRC support is optional */
   if (!dev->has_afrc)
      return false;

   /* AFRC<-->staging is expensive */
   if (pres->base.usage == PIPE_USAGE_STREAM)
      return false;

   /* Only a small selection of formats are AFRC'able */
   if (!pan_afrc_supports_format(fmt))
      return false;

   /* AFRC does not support layered (GLES3 style) multisampling. Use
    * EXT_multisampled_render_to_texture instead */
   if (pres->base.nr_samples > 1)
      return false;

   switch (pres->base.target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
   case PIPE_TEXTURE_3D:
      break;

   default:
      return false;
   }

   return true;
}

static uint64_t
panfrost_best_modifier(struct pipe_screen *pscreen,
                       const struct panfrost_resource *pres,
                       enum pipe_format fmt)
{
   struct panfrost_screen *screen = pan_screen(pscreen);
   struct panfrost_device *dev = pan_device(pscreen);

   /* Force linear textures when debugging tiling/compression */
   if (unlikely(dev->debug & PAN_DBG_LINEAR))
      return DRM_FORMAT_MOD_LINEAR;

   int afrc_rate = screen->force_afrc_rate;
   if (afrc_rate < 0)
      afrc_rate = pres->base.compression_rate;
   if (afrc_rate > PIPE_COMPRESSION_FIXED_RATE_NONE &&
       panfrost_should_afrc(dev, pres, fmt)) {
      /* It's not really possible to decide on a global AFRC-rate,
       * because the set of valid AFRC rates varies from format to
       * format. So instead, treat this as a minimum rate, and search
       * for the next valid one.
       */
      for (int i = afrc_rate; i < 12; ++i) {
         if (pan_afrc_get_modifiers(fmt, i, 0, NULL)) {
            afrc_rate = i;
            break;
         }
      }
   }

   if (afrc_rate != PIPE_COMPRESSION_FIXED_RATE_NONE &&
       panfrost_should_afrc(dev, pres, fmt)) {
      uint64_t mod;
      unsigned num_mods = 0;

      STATIC_ASSERT(PIPE_COMPRESSION_FIXED_RATE_DEFAULT == PAN_AFRC_RATE_DEFAULT);
      num_mods = pan_afrc_get_modifiers(fmt, afrc_rate, 1, &mod);
      if (num_mods > 0) {
         return mod;
      }
   }

   if (panfrost_should_afbc(dev, pres, fmt)) {
      uint64_t afbc = AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE;

      if (pan_afbc_can_ytr(pres->base.format))
         afbc |= AFBC_FORMAT_MOD_YTR;

      if (panfrost_should_tile_afbc(screen, pres))
         afbc |= AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC;

      return DRM_FORMAT_MOD_ARM_AFBC(afbc);
   } else if (panfrost_should_tile(dev, pres, fmt))
      return DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;
   else
      return DRM_FORMAT_MOD_LINEAR;
}

static bool
panfrost_should_checksum(const struct panfrost_device *dev,
                         const struct panfrost_resource *pres)
{
   /* When checksumming is enabled, the tile data must fit in the
    * size of the writeback buffer, so don't checksum formats
    * that use too much space. */

   unsigned bytes_per_pixel_max = (dev->arch == 6) ? 6 : 4;

   unsigned bytes_per_pixel = MAX2(pres->base.nr_samples, 1) *
                              util_format_get_blocksize(pres->base.format);

   return pres->base.bind & PIPE_BIND_RENDER_TARGET && panfrost_is_2d(pres) &&
          bytes_per_pixel <= bytes_per_pixel_max &&
          pres->base.last_level == 0 && !(dev->debug & PAN_DBG_NO_CRC);
}

static bool
panfrost_resource_try_setup(struct pipe_screen *screen,
                            struct panfrost_resource *pres, uint64_t modifier,
                            enum pipe_format fmt, unsigned plane_idx)
{
   struct panfrost_device *dev = pan_device(screen);
   uint64_t chosen_mod = modifier != DRM_FORMAT_MOD_INVALID
                            ? modifier
                            : panfrost_best_modifier(screen, pres, fmt);
   enum mali_texture_dimension dim =
      panfrost_translate_texture_dimension(pres->base.target);

   /* We can only switch tiled->linear if the resource isn't already
    * linear and if we control the modifier */
   pres->modifier_constant = !(chosen_mod != DRM_FORMAT_MOD_LINEAR &&
                               modifier == DRM_FORMAT_MOD_INVALID);

   /* Z32_S8X24 variants are actually stored in 2 planes (one per
    * component), we have to adjust the format on the first plane.
    */
   if (fmt == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)
      fmt = PIPE_FORMAT_Z32_FLOAT;

   pres->modifier = chosen_mod;

   bool h_subsamp = util_format_get_plane_width(fmt, plane_idx, 2) == 1;
   bool v_subsamp = util_format_get_plane_height(fmt, plane_idx, 2) == 1;
   struct pan_image_props iprops = {
      .modifier = panfrost_is_emulated_mod(chosen_mod) ? DRM_FORMAT_MOD_LINEAR
                                                       : chosen_mod,
      .format = fmt,
      .dim = dim,
      .extent_px = {
         /* pan_layout_init() wants the property of the full image, not the
          * plane, but pipe_resource encodes the properties of the plane.
          * We need to adjust the width/height according to the subsampling
          * properties. */
         .width = pres->base.width0 * (h_subsamp ? 2 : 1),
         .height = pres->base.height0 * (v_subsamp ? 2 : 1),
         .depth = pres->base.depth0,
      },
      .array_size = pres->base.array_size,
      .nr_samples = MAX2(pres->base.nr_samples, 1),
      .nr_slices = pres->base.last_level + 1,
      .crc = panfrost_should_checksum(dev, pres),
   };

   /* Update the compression rate with the correct value as we
    * want the real bitrate and not DEFAULT */
   pres->base.compression_rate = pan_afrc_get_rate(fmt, chosen_mod);

   return panfrost_resource_init_image(screen, pres, &iprops, plane_idx, NULL);
}

static void
panfrost_resource_setup(struct pipe_screen *screen,
                        struct panfrost_resource *pres, uint64_t modifier,
                        enum pipe_format fmt, unsigned plane_idx)
{
   ASSERTED bool valid =
      panfrost_resource_try_setup(screen, pres, modifier, fmt, plane_idx);

   assert(valid);
}

static int
panfrost_resource_init_afbc_headers(struct panfrost_resource *pres)
{
   if (panfrost_bo_mmap(pres->bo))
      return -1;

   for (unsigned i = 0; i < pres->base.array_size; ++i) {
      for (unsigned l = 0; l <= pres->base.last_level; ++l) {
         struct pan_image_slice_layout *slice = &pres->plane.layout.slices[l];
         unsigned z_slice_count = u_minify(pres->base.depth0, l);

         for (unsigned z = 0; z < z_slice_count; ++z) {
            void *ptr = pres->bo->ptr.cpu +
                        (i * pres->plane.layout.array_stride_B) +
                        slice->offset_B + (z * slice->afbc.surface_stride_B);

            /* Zero-ed AFBC headers seem to encode a plain
             * black. Let's use this pattern to keep the
             * initialization simple.
             */
            memset(ptr, 0, slice->afbc.header.surface_size_B);
         }
      }
   }
   return 0;
}

void
panfrost_resource_set_damage_region(struct pipe_screen *screen,
                                    struct pipe_resource *res,
                                    unsigned int nrects,
                                    const struct pipe_box *rects)
{
   struct panfrost_device *dev = pan_device(screen);
   struct panfrost_resource *pres = pan_resource(res);
   struct pipe_scissor_state *damage_extent = &pres->damage.extent;
   unsigned int i;

   /* Partial updates are implemented with a tile enable map only on v5.
    * Later architectures have a more efficient method of implementing
    * partial updates (frame shaders), while earlier architectures lack
    * tile enable maps altogether.
    */
   if (dev->arch == 5 && nrects > 1) {
      if (!pres->damage.tile_map.data) {
         pres->damage.tile_map.stride =
            ALIGN_POT(DIV_ROUND_UP(res->width0, 32 * 8), 64);
         pres->damage.tile_map.size =
            pres->damage.tile_map.stride * DIV_ROUND_UP(res->height0, 32);
         pres->damage.tile_map.data = malloc(pres->damage.tile_map.size);
      }

      memset(pres->damage.tile_map.data, 0, pres->damage.tile_map.size);
      pres->damage.tile_map.enable = true;
   } else {
      pres->damage.tile_map.enable = false;
   }

   /* Track the damage extent: the quad including all damage regions. Will
    * be used restrict the rendering area */

   damage_extent->minx = 0xffff;
   damage_extent->miny = 0xffff;

   unsigned enable_count = 0;

   for (i = 0; i < nrects; i++) {
      int x = rects[i].x, w = rects[i].width, h = rects[i].height;
      int y = res->height0 - (rects[i].y + h);

      damage_extent->minx = MIN2(damage_extent->minx, x);
      damage_extent->miny = MIN2(damage_extent->miny, y);
      damage_extent->maxx = MAX2(damage_extent->maxx, MIN2(x + w, res->width0));
      damage_extent->maxy =
         MAX2(damage_extent->maxy, MIN2(y + h, res->height0));

      if (!pres->damage.tile_map.enable)
         continue;

      unsigned t_x_start = x / 32;
      unsigned t_x_end = (x + w - 1) / 32;
      unsigned t_y_start = y / 32;
      unsigned t_y_end = (y + h - 1) / 32;

      for (unsigned t_y = t_y_start; t_y <= t_y_end; t_y++) {
         for (unsigned t_x = t_x_start; t_x <= t_x_end; t_x++) {
            unsigned b = (t_y * pres->damage.tile_map.stride * 8) + t_x;

            if (BITSET_TEST(pres->damage.tile_map.data, b))
               continue;

            BITSET_SET(pres->damage.tile_map.data, b);
            enable_count++;
         }
      }
   }

   if (nrects == 0) {
      damage_extent->minx = 0;
      damage_extent->miny = 0;
      damage_extent->maxx = res->width0;
      damage_extent->maxy = res->height0;
   }

   if (pres->damage.tile_map.enable) {
      unsigned t_x_start = damage_extent->minx / 32;
      unsigned t_x_end = damage_extent->maxx / 32;
      unsigned t_y_start = damage_extent->miny / 32;
      unsigned t_y_end = damage_extent->maxy / 32;
      unsigned tile_count =
         (t_x_end - t_x_start + 1) * (t_y_end - t_y_start + 1);

      /* Don't bother passing a tile-enable-map if the amount of
       * tiles to reload is to close to the total number of tiles.
       */
      if (tile_count - enable_count < 10)
         pres->damage.tile_map.enable = false;
   }
}

static bool
panfrost_can_create_resource(struct pipe_screen *screen,
                             const struct pipe_resource *template)
{
   struct panfrost_resource tmp;
   tmp.base = *template;

   if (!panfrost_resource_try_setup(screen, &tmp, DRM_FORMAT_MOD_INVALID,
                                    template->format, 0))
      return false;

   uint64_t system_memory;
   if (!os_get_total_physical_memory(&system_memory))
      return false;

   /* Limit maximum texture size to a quarter of the system memory, to avoid
    * allocating huge textures on systems with little memory.
    */
   return tmp.plane.layout.data_size_B <= system_memory / 4;
}

static struct pipe_resource *
panfrost_resource_create_with_modifier(struct pipe_screen *screen,
                                       const struct pipe_resource *template,
                                       uint64_t modifier, unsigned plane_idx)
{
   MESA_TRACE_FUNC();

   struct panfrost_device *dev = pan_device(screen);

   struct panfrost_resource *so = CALLOC_STRUCT(panfrost_resource);

   if (!so)
      return NULL;

   so->base = *template;
   so->base.screen = screen;

   pipe_reference_init(&so->base.reference, 1);

   util_range_init(&so->valid_buffer_range);

   if (template->bind & PAN_BIND_SHARED_MASK) {
      /* For compatibility with older consumers that may not be
       * modifiers aware, treat INVALID as LINEAR for shared
       * resources.
       */
      if (modifier == DRM_FORMAT_MOD_INVALID)
         modifier = DRM_FORMAT_MOD_LINEAR;

      /* At any rate, we can't change the modifier later for shared
       * resources, since we have no way to propagate the modifier
       * change.
       */
      so->modifier_constant = true;
   }

   panfrost_resource_setup(screen, so, modifier, template->format, plane_idx);

   if (dev->ro && (template->bind & PIPE_BIND_SCANOUT)) {
      struct winsys_handle handle;

      /* Block-based texture formats are only used for texture
       * compression (not framebuffer compression!), which doesn't
       * make sense to share across processes.
       */
      assert(util_format_get_blockwidth(template->format) == 1);
      assert(util_format_get_blockheight(template->format) == 1);

      /* Present a resource with similar dimensions that, if allocated
       * as a linear image, is big enough to fit the resource in the
       * actual layout. For linear images, this is a no-op. For 16x16
       * tiling, this aligns the dimensions to 16x16.
       *
       * For AFBC, this aligns the width to the superblock width (as
       * expected) and adds extra rows to account for the header. This
       * is a bit of a lie, but it's the best we can do with dumb
       * buffers, which are extremely not meant for AFBC. And yet this
       * has to work anyway...
       *
       * Moral of the story: if you're reading this comment, that
       * means you're working on WSI and so it's already too late for
       * you. I'm sorry.
       */
      unsigned stride = pan_image_get_wsi_row_pitch(&so->image, plane_idx, 0);
      enum pipe_format plane_format =
         util_format_get_plane_format(template->format, plane_idx);
      unsigned width = stride / util_format_get_blocksize(plane_format);
      unsigned size = so->plane.layout.data_size_B;
      unsigned effective_rows = DIV_ROUND_UP(size, stride);

      struct pipe_resource scanout_tmpl = {
         .target = so->base.target,
         .format = template->format,
         .width0 = width,
         .height0 = effective_rows,
         .depth0 = 1,
         .array_size = 1,
      };

      so->scanout =
         renderonly_scanout_for_resource(&scanout_tmpl, dev->ro, &handle);

      if (!so->scanout) {
         mesa_loge("Failed to create scanout resource\n");
         panfrost_resource_destroy(screen, &so->base);
         return NULL;
      }
      assert(handle.type == WINSYS_HANDLE_TYPE_FD);
      int ret = panfrost_resource_import_bo(so, dev, handle.handle);
      close(handle.handle);

      if (ret) {
         panfrost_resource_destroy(screen, &so->base);
         return NULL;
      }

      so->plane.base = so->bo->ptr.gpu;
   } else {
      /* We create a BO immediately but don't bother mapping, since we don't
       * care to map e.g. FBOs which the CPU probably won't touch */
      uint32_t flags = PAN_BO_DELAY_MMAP;

      /* If the resource is never exported, we can make the BO private. */
      if (template->bind & PIPE_BIND_SHARED)
         flags |= PAN_BO_SHAREABLE;

      char *res_label =
         panfrost_resource_new_label(so, so->image.props.modifier , NULL);

      so->bo =
         panfrost_bo_create(dev, so->plane.layout.data_size_B, flags, res_label);

      if (!so->bo) {
         panfrost_resource_destroy(screen, &so->base);
         return NULL;
      }

      so->plane.base = so->bo->ptr.gpu;

      so->constant_stencil = true;
      so->owns_label = true;
   }

   if (drm_is_afbc(so->modifier)) {
      if (panfrost_resource_init_afbc_headers(so)) {
         panfrost_resource_destroy(screen, &so->base);
         return NULL;
      }
   }

   panfrost_resource_set_damage_region(screen, &so->base, 0, NULL);

   if (template->bind & PIPE_BIND_INDEX_BUFFER)
      so->index_cache = CALLOC_STRUCT(pan_minmax_cache);

   return (struct pipe_resource *)so;
}

/* Default is to create a resource as don't care */

static struct pipe_resource *
panfrost_resource_create(struct pipe_screen *screen,
                         const struct pipe_resource *template)
{
   return panfrost_resource_create_with_modifier(screen, template,
                                                 DRM_FORMAT_MOD_INVALID, 0);
}

/* If no modifier is specified, we'll choose. Otherwise, the order of
 * preference is compressed, tiled, linear. */

static struct pipe_resource *
panfrost_resource_create_with_modifiers(struct pipe_screen *screen,
                                        const struct pipe_resource *template,
                                        const uint64_t *modifiers, int count)
{
   PANFROST_EMULATED_MODIFIERS(emulated_mods);
   PAN_SUPPORTED_MODIFIERS(native_mods);

   for (unsigned i = 0; i < ARRAY_SIZE(native_mods); ++i) {
      if (drm_find_modifier(native_mods[i], modifiers, count)) {
         return panfrost_resource_create_with_modifier(screen, template,
                                                       native_mods[i], 0);
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(emulated_mods); ++i) {
      if (drm_find_modifier(emulated_mods[i], modifiers, count)) {
         return panfrost_resource_create_with_modifier(screen, template,
                                                       emulated_mods[i], 0);
      }
   }

   /* If we didn't find one, app specified invalid */
   assert(count == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID);
   return panfrost_resource_create(screen, template);
}

void
panfrost_resource_change_format(struct panfrost_resource *rsrc,
                                enum pipe_format new_format,
                                struct panfrost_resource *save)
{
   if (!rsrc)
      return;

   assert(rsrc->image.props.modifier == DRM_FORMAT_MOD_LINEAR);
   assert(util_format_get_num_planes(new_format) == 1);
   assert(util_format_get_blockwidth(new_format) == 1 &&
          util_format_get_blockheight(new_format) == 1);
   assert(util_format_get_blockwidth(rsrc->image.props.format) == 1 &&
          util_format_get_blockheight(rsrc->image.props.format) == 1);

   if (new_format == rsrc->image.props.format)
      return;

   *save = *rsrc;

   unsigned old_res_plane_idx = pan_resource_plane_index(rsrc);
   enum pipe_format old_format =
      util_format_get_plane_format(rsrc->image.props.format, old_res_plane_idx);
   unsigned old_width =
      util_format_get_plane_width(rsrc->image.props.format, old_res_plane_idx,
                                  rsrc->image.props.extent_px.width);

   unsigned old_fmt_blksize = util_format_get_blocksize(old_format);
   unsigned new_fmt_blksize = util_format_get_blocksize(new_format);

   if (old_fmt_blksize != new_fmt_blksize) {
      assert((old_fmt_blksize * rsrc->base.width0) % new_fmt_blksize == 0);
      rsrc->base.width0 =
         (old_fmt_blksize * rsrc->base.width0) / new_fmt_blksize;
      rsrc->image.props.extent_px.width =
         (old_fmt_blksize * old_width) /
         new_fmt_blksize;
      rsrc->image.props.extent_px.height =
         util_format_get_plane_height(rsrc->image.props.format, old_res_plane_idx,
                                      rsrc->image.props.extent_px.height);
   }

   rsrc->base.next = NULL;
   rsrc->base.format = new_format;
   rsrc->image.props.format = new_format;
   rsrc->image.planes[0] = &rsrc->plane;
   rsrc->image.planes[1] = NULL;
   rsrc->image.planes[2] = NULL;
}

void
panfrost_resource_restore_format(struct panfrost_resource *rsrc,
                                 const struct panfrost_resource *saved)
{
   if (!rsrc)
      return;

   rsrc->base.next = saved->base.next;
   memcpy(rsrc->image.planes, saved->image.planes, sizeof(rsrc->image.planes));
   rsrc->base.format = saved->base.format;
   rsrc->image.props.format = saved->image.props.format;
   rsrc->base.width0 = saved->base.width0;
   rsrc->image.props.extent_px.width = saved->image.props.extent_px.width;
   rsrc->image.props.extent_px.height = saved->image.props.extent_px.height;
}

/* Most of the time we can do CPU-side transfers, but sometimes we need to use
 * the 3D pipe for this. Let's wrap u_blitter to blit to/from staging textures.
 * Code adapted from freedreno */

static struct panfrost_resource *
pan_alloc_staging(struct panfrost_context *ctx, struct panfrost_resource *rsc,
                  unsigned level, const struct pipe_box *box)
{
   struct pipe_context *pctx = &ctx->base;
   struct pipe_resource tmpl = rsc->base;

   tmpl.width0 = box->width;
   tmpl.height0 = box->height;
   /* for array textures, box->depth is the array_size, otherwise
    * for 3d textures, it is the depth:
    */
   if (tmpl.array_size > 1) {
      if (tmpl.target == PIPE_TEXTURE_CUBE)
         tmpl.target = PIPE_TEXTURE_2D_ARRAY;
      tmpl.array_size = box->depth;
      tmpl.depth0 = 1;
   } else {
      tmpl.array_size = 1;
      tmpl.depth0 = box->depth;
   }
   tmpl.last_level = 0;
   tmpl.bind |= PIPE_BIND_LINEAR;
   tmpl.bind &= ~PAN_BIND_SHARED_MASK;
   tmpl.compression_rate = PIPE_COMPRESSION_FIXED_RATE_NONE;

   struct pipe_resource *pstaging =
      pctx->screen->resource_create(pctx->screen, &tmpl);
   if (!pstaging)
      return NULL;

   return pan_resource(pstaging);
}

static void
pan_blit_from_staging(struct pipe_context *pctx,
                      struct panfrost_transfer *trans)
{
   struct pipe_resource *dst = trans->base.resource;
   struct pipe_blit_info blit = {0};

   blit.dst.resource = dst;
   blit.dst.format = dst->format;
   blit.dst.level = trans->base.level;
   blit.dst.box = trans->base.box;
   blit.src.resource = trans->staging.rsrc;
   blit.src.format = trans->staging.rsrc->format;
   blit.src.level = 0;
   blit.src.box = trans->staging.box;
   blit.mask = util_format_get_mask(blit.src.format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;

   panfrost_blit_no_afbc_legalization(pctx, &blit);
}

static void
pan_blit_to_staging(struct pipe_context *pctx, struct panfrost_transfer *trans)
{
   struct pipe_resource *src = trans->base.resource;
   struct pipe_blit_info blit = {0};

   blit.src.resource = src;
   blit.src.format = src->format;
   blit.src.level = trans->base.level;
   blit.src.box = trans->base.box;
   blit.dst.resource = trans->staging.rsrc;
   blit.dst.format = trans->staging.rsrc->format;
   blit.dst.level = 0;
   blit.dst.box = trans->staging.box;
   blit.mask = util_format_get_mask(blit.dst.format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;

   panfrost_blit_no_afbc_legalization(pctx, &blit);
}

static void
panfrost_load_tiled_images(struct panfrost_transfer *transfer,
                           struct panfrost_resource *rsrc)
{
   struct pipe_transfer *ptrans = &transfer->base;
   unsigned level = ptrans->level;

   /* If the requested level of the image is uninitialized, it's not
    * necessary to copy it. Leave the result unintiialized too.
    */
   if (!BITSET_TEST(rsrc->valid.data, level))
      return;

   struct panfrost_bo *bo = rsrc->bo;
   unsigned stride =
      rsrc->image.props.dim == MALI_TEXTURE_DIMENSION_3D
         ? rsrc->plane.layout.slices[level].tiled_or_linear.surface_stride_B
         : rsrc->plane.layout.array_stride_B;

   /* Otherwise, load each layer separately, required to load from 3D and
    * array textures.
    */
   for (unsigned z = 0; z < ptrans->box.depth; ++z) {
      void *dst = transfer->map + (ptrans->layer_stride * z);
      uint8_t *map = bo->ptr.cpu + rsrc->plane.layout.slices[level].offset_B +
                     (z + ptrans->box.z) * stride;

      pan_load_tiled_image(
         dst, map, ptrans->box.x, ptrans->box.y, ptrans->box.width,
         ptrans->box.height, ptrans->stride,
         rsrc->plane.layout.slices[level].tiled_or_linear.row_stride_B,
         rsrc->image.props.format, PAN_INTERLEAVE_NONE);
   }
}

#if MESA_DEBUG

static void
dump_headerblock(struct panfrost_resource *rsrc, uint32_t idx)
{
   panfrost_bo_wait(rsrc->bo, INT64_MAX, false);

   uint8_t *ptr = rsrc->bo->ptr.cpu;
   struct pan_afbc_headerblock *header = (struct pan_afbc_headerblock *)
      (ptr + (idx * AFBC_HEADER_BYTES_PER_TILE));
   uint32_t *header_u32 = (uint32_t *)header;
   uint32_t *body = (uint32_t *)(ptr + header->payload.offset);
   struct pan_image_block_size block_sz =
      pan_afbc_subblock_size(rsrc->modifier);
   unsigned pixel_sz = util_format_get_blocksize(rsrc->base.format);
   unsigned uncompressed_size = pixel_sz * block_sz.width * block_sz.height;
   uint32_t size = pan_afbc_payload_size(7, *header, uncompressed_size);

   fprintf(stderr, "  Header: %08x %08x %08x %08x (size: %u bytes)\n",
           header_u32[0], header_u32[1], header_u32[2], header_u32[3], size);
   if (size > 0) {
      fprintf(stderr, "  Body:   %08x %08x %08x %08x\n", body[0], body[1],
              body[2], body[3]);
   } else {
      fprintf(stderr, "  Color:  0x%02x%02x%02x%02x\n",
              header->color.rgba8888.r, header->color.rgba8888.g,
              header->color.rgba8888.b, header->color.rgba8888.a);
   }
   fprintf(stderr, "\n");
}

void
pan_dump_resource(struct panfrost_context *ctx, struct panfrost_resource *rsc)
{
   struct pipe_context *pctx = &ctx->base;
   struct pipe_resource tmpl = rsc->base;
   struct pipe_resource *plinear = NULL;
   struct panfrost_resource *linear = rsc;
   struct pipe_blit_info blit = {0};
   struct pipe_box box;
   char buffer[1024];

   if (rsc->modifier != DRM_FORMAT_MOD_LINEAR) {
      tmpl.bind |= PIPE_BIND_LINEAR;
      tmpl.bind &= ~PAN_BIND_SHARED_MASK;

      plinear = pctx->screen->resource_create(pctx->screen, &tmpl);
      u_box_2d(0, 0, rsc->base.width0, rsc->base.height0, &box);

      blit.src.resource = &rsc->base;
      blit.src.format = rsc->base.format;
      blit.src.level = 0;
      blit.src.box = box;
      blit.dst.resource = plinear;
      blit.dst.format = rsc->base.format;
      blit.dst.level = 0;
      blit.dst.box = box;
      blit.mask = util_format_get_mask(blit.dst.format);
      blit.filter = PIPE_TEX_FILTER_NEAREST;

      panfrost_blit(pctx, &blit);

      linear = pan_resource(plinear);
   }

   panfrost_flush_writer(ctx, linear, "dump image");
   panfrost_bo_wait(linear->bo, INT64_MAX, false);

   if (!panfrost_bo_mmap(linear->bo)) {
      static unsigned frame_count = 0;
      frame_count++;
      snprintf(buffer, sizeof(buffer), "dump_image.%04d", frame_count);

      debug_dump_image(
         buffer, rsc->base.format, 0 /* UNUSED */, rsc->base.width0,
         rsc->base.height0,
         linear->plane.layout.slices[0].tiled_or_linear.row_stride_B,
         linear->bo->ptr.cpu);
   } else {
      mesa_loge("failed to mmap, not dumping resource");
   }

   if (plinear)
      pipe_resource_reference(&plinear, NULL);
}

#endif

static void
panfrost_store_tiled_images(struct panfrost_transfer *transfer,
                            struct panfrost_resource *rsrc)
{
   struct panfrost_bo *bo = rsrc->bo;
   struct pipe_transfer *ptrans = &transfer->base;
   unsigned level = ptrans->level;
   unsigned stride =
      rsrc->image.props.dim == MALI_TEXTURE_DIMENSION_3D
         ? rsrc->plane.layout.slices[level].tiled_or_linear.surface_stride_B
         : rsrc->plane.layout.array_stride_B;

   /* Otherwise, store each layer separately, required to store to 3D and
    * array textures.
    */
   for (unsigned z = 0; z < ptrans->box.depth; ++z) {
      void *src = transfer->map + (ptrans->layer_stride * z);
      uint8_t *map = bo->ptr.cpu + rsrc->plane.layout.slices[level].offset_B +
                     (z + ptrans->box.z) * stride;

      pan_store_tiled_image(
         map, src, ptrans->box.x, ptrans->box.y, ptrans->box.width,
         ptrans->box.height,
         rsrc->plane.layout.slices[level].tiled_or_linear.row_stride_B,
         ptrans->stride, rsrc->image.props.format, PAN_INTERLEAVE_NONE);
   }
}

static bool
panfrost_box_covers_resource(const struct pipe_resource *resource,
                             const struct pipe_box *box)
{
   return resource->last_level == 0 &&
          util_texrange_covers_whole_level(resource, 0, box->x, box->y, box->z,
                                           box->width, box->height, box->depth);
}

static bool
panfrost_can_discard(struct pipe_resource *resource, const struct pipe_box *box,
                     unsigned usage)
{
   struct panfrost_resource *rsrc = pan_resource(resource);

   return ((usage & PIPE_MAP_DISCARD_RANGE) &&
           !(usage & PIPE_MAP_UNSYNCHRONIZED) &&
           !(resource->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT) &&
           panfrost_box_covers_resource(resource, box) &&
           !(rsrc->bo->flags & PAN_BO_SHARED));
}

static void *
panfrost_ptr_map(struct pipe_context *pctx, struct pipe_resource *resource,
                 unsigned level,
                 unsigned usage, /* a combination of PIPE_MAP_x */
                 const struct pipe_box *box,
                 struct pipe_transfer **out_transfer)
{
   MESA_TRACE_FUNC();

   struct panfrost_context *ctx = pan_context(pctx);
   struct panfrost_device *dev = pan_device(pctx->screen);
   struct panfrost_resource *rsrc = pan_resource(resource);
   enum pipe_format format = rsrc->image.props.format;
   int bytes_per_block = util_format_get_blocksize(format);
   struct panfrost_bo *bo = rsrc->bo;

   /* Can't map tiled/compressed directly */
   if ((usage & PIPE_MAP_DIRECTLY) &&
       rsrc->modifier != DRM_FORMAT_MOD_LINEAR)
      return NULL;

   struct panfrost_transfer *transfer = rzalloc(pctx, struct panfrost_transfer);
   transfer->base.level = level;
   transfer->base.usage = usage;
   transfer->base.box = *box;

   pipe_resource_reference(&transfer->base.resource, resource);
   *out_transfer = &transfer->base;

   if (usage & PIPE_MAP_WRITE)
      rsrc->constant_stencil = false;

   /* We don't have s/w routines for AFBC/AFRC, so use a staging texture */
   if (drm_is_afbc(rsrc->modifier) ||
       drm_is_afrc(rsrc->modifier)) {
      struct panfrost_resource *staging =
         pan_alloc_staging(ctx, rsrc, level, box);
      assert(staging);

      /* Staging resources have one LOD: level 0. Query the strides
       * on this LOD.
       */
      transfer->base.stride =
         staging->plane.layout.slices[0].tiled_or_linear.row_stride_B;
      transfer->base.layer_stride =
         staging->image.props.dim == MALI_TEXTURE_DIMENSION_3D
            ? staging->plane.layout.slices[0].tiled_or_linear.surface_stride_B
            : staging->plane.layout.array_stride_B;

      transfer->staging.rsrc = &staging->base;

      transfer->staging.box = *box;
      transfer->staging.box.x = 0;
      transfer->staging.box.y = 0;
      transfer->staging.box.z = 0;

      assert(transfer->staging.rsrc != NULL);

      bool valid = BITSET_TEST(rsrc->valid.data, level);

      if ((usage & PIPE_MAP_READ) &&
          (valid || panfrost_any_batch_writes_rsrc(ctx, rsrc))) {
         pan_blit_to_staging(pctx, transfer);
         panfrost_flush_writer(ctx, staging, "AFBC/AFRC tex read staging blit");
         panfrost_bo_wait(staging->bo, INT64_MAX, false);
      }

      if (panfrost_bo_mmap(staging->bo))
         return NULL;

      return staging->bo->ptr.cpu;
   }

   bool already_mapped = bo->ptr.cpu != NULL;

   /* If we haven't already mmaped, now's the time */
   if (panfrost_bo_mmap(bo))
      return NULL;

   if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) {
      pandecode_inject_mmap(dev->decode_ctx, bo->ptr.gpu, bo->ptr.cpu,
                            panfrost_bo_size(bo), NULL);
   }

   /* Upgrade writes to uninitialized ranges to UNSYNCHRONIZED */
   if ((usage & PIPE_MAP_WRITE) && resource->target == PIPE_BUFFER &&
       !util_ranges_intersect(&rsrc->valid_buffer_range, box->x,
                              box->x + box->width)) {

      usage |= PIPE_MAP_UNSYNCHRONIZED;
   }

   /* Upgrade DISCARD_RANGE to WHOLE_RESOURCE if the whole resource is
    * being mapped.
    */
   if (panfrost_can_discard(resource, box, usage)) {
      usage |= PIPE_MAP_DISCARD_WHOLE_RESOURCE;
   }

   bool create_new_bo = usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE;
   bool copy_resource = false;

   if (!(usage & PIPE_MAP_UNSYNCHRONIZED) &&
       !(resource->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT) &&
       (usage & PIPE_MAP_WRITE) && panfrost_any_batch_reads_rsrc(ctx, rsrc)) {
      /* When a resource to be modified is already being used by a
       * pending batch, it is often faster to copy the whole BO than
       * to flush and split the frame in two.
       */

      panfrost_flush_writer(ctx, rsrc, "Shadow resource creation");
      panfrost_bo_wait(bo, INT64_MAX, false);

      create_new_bo = true;
      copy_resource = !(usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE);
   }

   /* Shadowing with separate stencil may require additional accounting.
    * Bail in these exotic cases.
    */
   if (rsrc->separate_stencil || rsrc->shadow_image) {
      create_new_bo = false;
      copy_resource = false;
   }

   if (create_new_bo &&
       (!(resource->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT) ||
        !already_mapped)) {
      /* Make sure we re-emit any descriptors using this resource */
      panfrost_dirty_state_all(ctx);

      /* If the BO is used by one of the pending batches or if it's
       * not ready yet (still accessed by one of the already flushed
       * batches), we try to allocate a new one to avoid waiting.
       */
      if (panfrost_any_batch_reads_rsrc(ctx, rsrc) ||
          !panfrost_bo_wait(bo, 0, true)) {
         /* We want the BO to be MMAPed. */
         uint32_t flags = bo->flags & ~PAN_BO_DELAY_MMAP;
         struct panfrost_bo *newbo = NULL;

         /* When the BO has been imported/exported, we can't
          * replace it by another one, otherwise the
          * importer/exporter wouldn't see the change we're
          * doing to it.
          */
         if (!(bo->flags & PAN_BO_SHARED)) {
            newbo =
               panfrost_bo_create(dev, panfrost_bo_size(bo), flags, bo->label);
         }

         if (newbo) {
            if (copy_resource) {
               memcpy(newbo->ptr.cpu, rsrc->bo->ptr.cpu, panfrost_bo_size(bo));
            }

            /* Swap the pointers, dropping a reference to
             * the old BO which is no long referenced from
             * the resource.
             */
            panfrost_bo_unreference(rsrc->bo);
            rsrc->bo = newbo;
            rsrc->plane.base = newbo->ptr.gpu;

            if (!copy_resource && drm_is_afbc(rsrc->modifier)) {
               if (panfrost_resource_init_afbc_headers(rsrc))
                  return NULL;
            }

            bo = newbo;
         } else {
            /* Allocation failed or was impossible, let's
             * fall back on a flush+wait.
             */
            panfrost_flush_batches_accessing_rsrc(
               ctx, rsrc, "Resource access with high memory pressure");
            panfrost_bo_wait(bo, INT64_MAX, true);
         }
      }
   } else if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
      if (usage & PIPE_MAP_WRITE) {
         panfrost_flush_batches_accessing_rsrc(ctx, rsrc, "Synchronized write");
         panfrost_bo_wait(bo, INT64_MAX, true);
      } else if (usage & PIPE_MAP_READ) {
         panfrost_flush_writer(ctx, rsrc, "Synchronized read");
         panfrost_bo_wait(bo, INT64_MAX, false);
      }
   }

   /* For access to compressed textures, we want the (x, y, w, h)
    * region-of-interest in blocks, not pixels. Then we compute the stride
    * between rows of blocks as the width in blocks times the width per
    * block, etc.
    */
   struct pipe_box box_blocks;
   u_box_pixels_to_blocks(&box_blocks, box, format);

   switch(rsrc->modifier) {
   case DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED:
   case DRM_FORMAT_MOD_MTK_16L_32S_TILE:
      transfer->base.stride = box_blocks.width * bytes_per_block;
      transfer->base.layer_stride = transfer->base.stride * box_blocks.height;
      transfer->map =
         ralloc_size(transfer, transfer->base.layer_stride * box->depth);

      if (usage & PIPE_MAP_READ)
         panfrost_load_tiled_images(transfer, rsrc);

      return transfer->map;
   default:
      assert(rsrc->modifier == DRM_FORMAT_MOD_LINEAR);

      /* Direct, persistent writes create holes in time for
       * caching... I don't know if this is actually possible but we
       * should still get it right */

      unsigned dpw = PIPE_MAP_DIRECTLY | PIPE_MAP_WRITE | PIPE_MAP_PERSISTENT;

      if ((usage & dpw) == dpw && rsrc->index_cache)
         return NULL;

      transfer->base.stride =
         rsrc->plane.layout.slices[level].tiled_or_linear.row_stride_B;
      transfer->base.layer_stride =
         rsrc->image.props.dim == MALI_TEXTURE_DIMENSION_3D
            ? rsrc->plane.layout.slices[level].tiled_or_linear.surface_stride_B
            : rsrc->plane.layout.array_stride_B;

      /* By mapping direct-write, we're implicitly already
       * initialized (maybe), so be conservative */

      if (usage & PIPE_MAP_WRITE) {
         BITSET_SET(rsrc->valid.data, level);
         pan_minmax_cache_invalidate(
            rsrc->index_cache, util_format_get_blocksize(rsrc->base.format),
            transfer->base.box.x, transfer->base.box.width);
      }

      return bo->ptr.cpu + rsrc->plane.layout.slices[level].offset_B +
             box->z * transfer->base.layer_stride +
             box_blocks.y *
                rsrc->plane.layout.slices[level].tiled_or_linear.row_stride_B +
             box_blocks.x * bytes_per_block;
   }
}

void
pan_resource_modifier_convert(struct panfrost_context *ctx,
                              struct panfrost_resource *rsrc, uint64_t modifier,
                              bool copy_resource, const char *reason)
{
   MESA_TRACE_FUNC();

   bool need_shadow = rsrc->modifier_constant;

   assert(!rsrc->modifier_constant || copy_resource);

   struct pipe_resource templates[MAX_IMAGE_PLANES] = {0};
   unsigned plane_count;

   pan_resource_afbcp_stop(rsrc);

   templates[0] = rsrc->base;
   for (plane_count = 1;
        templates[plane_count - 1].next && plane_count < ARRAY_SIZE(templates);
        plane_count++)
      templates[plane_count] = *(templates[plane_count - 1].next);

   struct panfrost_resource *tmp_rsrc = NULL;

   for (int i = plane_count - 1; i >= 0; i--) {
      if (tmp_rsrc)
         templates[i].next = &tmp_rsrc->base;

      struct pipe_resource *new_prsrc =
         panfrost_resource_create_with_modifier(ctx->base.screen, &templates[i],
                                                modifier, i);

      tmp_rsrc = pan_resource(new_prsrc);
   }

   if (need_shadow && rsrc->shadow_image) {
      /* free the old shadow image */
      pipe_resource_reference(
         (struct pipe_resource **)&rsrc->shadow_image, NULL);
   }
   if (copy_resource) {
      struct pipe_blit_info blit = {
         .dst.resource = &tmp_rsrc->base,
         .dst.format = tmp_rsrc->base.format,
         .src.resource = &rsrc->base,
         .src.format = rsrc->base.format,
         .mask = util_format_get_mask(tmp_rsrc->base.format),
         .filter = PIPE_TEX_FILTER_NEAREST,
      };

      struct panfrost_screen *screen = pan_screen(ctx->base.screen);
      /* data_valid is not valid until flushed */
      panfrost_flush_writer(ctx, rsrc, "AFBC/AFRC decompressing blit");

      for (int i = 0; i <= rsrc->base.last_level; i++) {
         if (BITSET_TEST(rsrc->valid.data, i)) {
            blit.dst.level = blit.src.level = i;

            u_box_3d(0, 0, 0, u_minify(rsrc->base.width0, i),
                     u_minify(rsrc->base.height0, i),
                     util_num_layers(&rsrc->base, i), &blit.dst.box);
            blit.src.box = blit.dst.box;

            if (drm_is_mtk_tiled(rsrc->modifier))
               screen->vtbl.mtk_detile(ctx, &blit);
            else
               panfrost_blit_no_afbc_legalization(&ctx->base, &blit);
         }
      }

      /* we lose track of tmp_rsrc after this point, and the BO migration
       * (from tmp_rsrc to rsrc) doesn't transfer the last_writer to rsrc
       */
      panfrost_flush_writer(ctx, tmp_rsrc, "AFBC/AFRC decompressing blit");
   }

   if (need_shadow) {
      panfrost_resource_setup(ctx->base.screen, tmp_rsrc,
                              modifier, tmp_rsrc->base.format, 0);
      rsrc->shadow_image = tmp_rsrc;
   } else {
      if (rsrc->owns_label) {
         char *old_label = (char *)panfrost_bo_replace_label(rsrc->bo, "Disposed old modifier BO", false);
         free(old_label);
      }
      panfrost_bo_unreference(rsrc->bo);

      rsrc->bo = tmp_rsrc->bo;
      rsrc->plane.base = rsrc->bo->ptr.gpu;
      panfrost_bo_reference(rsrc->bo);

      rsrc->owns_label = tmp_rsrc->owns_label;
      tmp_rsrc->owns_label = false;

      panfrost_resource_setup(ctx->base.screen, rsrc, modifier,
                              tmp_rsrc->base.format, 0);
      /* panfrost_resource_setup will force the modifier to stay constant when
       * called with a specific modifier. We don't want that here, we want to
       * be able to convert back to another modifier if needed */
      rsrc->modifier_constant = false;

      struct pipe_resource *tmp_prsrc = &tmp_rsrc->base;

      pipe_resource_reference(&tmp_prsrc, NULL);

      perf_debug(ctx, "resource_modifier_convert required due to: %s", reason);
   }
}

/* Validate that an AFBC/AFRC resource may be used as a particular format. If it
 * may not, decompress it on the fly. Failure to do so can produce wrong results
 * or invalid data faults when sampling or rendering to AFBC */

void
pan_legalize_format(struct panfrost_context *ctx,
                    struct panfrost_resource *rsrc, enum pipe_format format,
                    bool write, bool discard)
{
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   enum pipe_format old_format = rsrc->base.format;
   enum pipe_format new_format = format;
   bool compatible = true;
   uint64_t dest_modifier = DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;

   if (!drm_is_afbc(rsrc->modifier) &&
       !drm_is_afrc(rsrc->modifier) &&
       !drm_is_mtk_tiled(rsrc->modifier))
      return;

   if (drm_is_afbc(rsrc->modifier)) {
      compatible = (pan_afbc_format(dev->arch, old_format, 0) ==
                    pan_afbc_format(dev->arch, new_format, 0));
   } else if (drm_is_afrc(rsrc->modifier)) {
      struct pan_afrc_format_info old_info =
         pan_afrc_get_format_info(old_format);
      struct pan_afrc_format_info new_info =
         pan_afrc_get_format_info(new_format);
      compatible = !memcmp(&old_info, &new_info, sizeof(old_info));
   } else if (drm_is_mtk_tiled(rsrc->modifier)) {
      compatible = false;
      dest_modifier = DRM_FORMAT_MOD_LINEAR;
   }

   if (!compatible) {
      pan_resource_modifier_convert(
         ctx, rsrc, dest_modifier, !discard,
         drm_is_afbc(rsrc->modifier)
            ? "Reinterpreting AFBC surface as incompatible format"
            : "Reinterpreting tiled surface as incompatible format");
      return;
   }

   /* Can't write to AFBC-P resources */
   if (write && drm_is_afbc(rsrc->modifier) &&
       (rsrc->modifier & AFBC_FORMAT_MOD_SPARSE) == 0) {
      pan_resource_modifier_convert(
         ctx, rsrc, rsrc->modifier | AFBC_FORMAT_MOD_SPARSE, !discard,
         "Legalizing resource to allow writing");
   }
}

static bool
panfrost_should_linear_convert(struct panfrost_context *ctx,
                               struct panfrost_resource *prsrc,
                               struct pipe_transfer *transfer)
{
   if (prsrc->modifier_constant)
      return false;

   /* Overwriting the entire resource indicates streaming, for which
    * linear layout is most efficient due to the lack of expensive
    * conversion.
    *
    * For now we just switch to linear after a number of complete
    * overwrites to keep things simple, but we could do better.
    *
    * This mechanism is only implemented for 2D resources. This suffices
    * for video players, its intended use case.
    */

   bool entire_overwrite = panfrost_is_2d(prsrc) &&
                           prsrc->base.last_level == 0 &&
                           transfer->box.width == prsrc->base.width0 &&
                           transfer->box.height == prsrc->base.height0 &&
                           transfer->box.x == 0 && transfer->box.y == 0;

   if (entire_overwrite)
      ++prsrc->modifier_updates;

   if (prsrc->modifier_updates >= LAYOUT_CONVERT_THRESHOLD) {
      perf_debug(ctx, "Transitioning to linear due to streaming usage");
      return true;
   } else {
      return false;
   }
}

/* Queue a CS modifier conversion job to calculate and store the payload sizes
 * of the current AFBC BO into a new AFBC-P payload layout BO. Returns true on
 * success. */
static bool
pan_resource_afbcp_get_payload_sizes(struct panfrost_context *ctx,
                                     struct panfrost_resource *prsrc)
{
   MESA_TRACE_FUNC();

   afbcp_debug(ctx,
               "AFBC-P prsrc=%p: Get payload sizes (reads=%u bo_size=%zu, gpu=%s)",
               prsrc, prsrc->afbcp->nr_consecutive_reads,
               panfrost_bo_size(prsrc->bo),
               pan_screen(ctx->base.screen)->afbcp_gpu_payload_sizes ?
               "true" : "false");

   struct panfrost_screen *screen = pan_screen(ctx->base.screen);
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   enum pipe_format format = prsrc->base.format;
   uint64_t modifier = prsrc->modifier;
   unsigned last_level = prsrc->base.last_level;
   unsigned layout_size = 0;

   for (unsigned level = 0; level <= last_level; ++level) {
      struct pan_image_slice_layout *slice =
         &prsrc->plane.layout.slices[level];
      unsigned nr_blocks =
         pan_afbc_stride_blocks(
            format, modifier, slice->afbc.header.row_stride_B) *
         pan_afbc_height_blocks(
            format, modifier, u_minify(prsrc->image.props.extent_px.height, level));
      prsrc->afbcp->layout_offsets[level] = layout_size;
      layout_size += nr_blocks * sizeof(struct pan_afbc_payload_extent);
   }

   prsrc->afbcp->layout_bo = panfrost_bo_create(
      dev, layout_size, 0, "AFBC-P payload layout");
   if (!prsrc->afbcp->layout_bo) {
      mesa_loge("pan_resource_afbcp_get_payload_sizes: failed to create "
                "AFBC-P payload layout BO");
      return false;
   }

   if (!pan_screen(ctx->base.screen)->afbcp_gpu_payload_sizes)
      return true;

   prsrc->afbcp->skip_access_updates = true;

   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
   for (unsigned level = 0; level <= last_level; ++level)
      screen->vtbl.afbc_size(batch, prsrc, prsrc->afbcp->layout_bo,
                             prsrc->afbcp->layout_offsets[level], level);

   prsrc->afbcp->skip_access_updates = false;

   return true;
}

static uint32_t
pan_resource_afbcp_get_payload_layout(struct panfrost_context *ctx,
                                      struct panfrost_resource *prsrc,
                                      struct pan_afbc_payload_extent *layout,
                                      uint32_t nr_blocks_total,
                                      uint32_t header_offset)
{
   if (!pan_screen(ctx->base.screen)->afbcp_gpu_payload_sizes) {
      /* The CPU version sets both the payload sizes and offsets at once. */
      struct panfrost_device *dev = pan_device(ctx->base.screen);
      struct pan_afbc_headerblock *headers = (struct pan_afbc_headerblock *)
         ((uint8_t *)prsrc->bo->ptr.cpu + header_offset);
      return pan_afbc_payload_layout_packed(
         dev->arch, headers, layout, nr_blocks_total, prsrc->base.format,
         prsrc->modifier);
   }

   /* Stack allocated chunk used to copy the AFBC-P payload layout from
    * non-cacheable memory to cacheable memory. Each iteration of the offset
    * computation loop below otherwise forces a flush of the write combining
    * buffer because of the 32-bit read interleaved with the 32-bit write. A
    * tile is composed of 8x8 header blocks. A chunk is made of 16 tiles so
    * that at most 8 kB can be copied at each iteration (smaller values tend
    * to increase latency). */
   alignas(16) struct pan_afbc_payload_extent layout_chunk[64 * 16];
   unsigned nr_blocks_per_chunk = ARRAY_SIZE(layout_chunk);
   uint32_t body_size_B = 0;

   for (unsigned i = 0; i < nr_blocks_total; i += nr_blocks_per_chunk) {
      unsigned nr_blocks =  MIN2(nr_blocks_per_chunk, nr_blocks_total - i);

      util_streaming_load_memcpy(
         layout_chunk, &layout[i],
         nr_blocks * sizeof(struct pan_afbc_payload_extent));

      for (unsigned j = 0; j < nr_blocks; j++) {
         layout[i + j].offset = body_size_B;
         body_size_B += layout_chunk[j].size;
      }
   }

   return body_size_B;
}

/* Calculate and store the packed payload offsets into the AFBC-P payload
 * layout BO and store the total packed size. */
static void
pan_resource_afbcp_get_payload_offsets(struct panfrost_context *ctx,
                                       struct panfrost_resource *prsrc)
{
   MESA_TRACE_FUNC();

   afbcp_debug(ctx,
               "AFBC-P prsrc=%p: Get payload offsets (reads=%u bo_size=%zu)",
               prsrc, prsrc->afbcp->nr_consecutive_reads,
               panfrost_bo_size(prsrc->bo));

   struct panfrost_device *dev = pan_device(ctx->base.screen);
   uint64_t modifier = prsrc->modifier;
   enum pipe_format format = prsrc->base.format;
   unsigned last_level = prsrc->base.last_level;
   unsigned total_size = 0;

   for (unsigned level = 0; level <= last_level; ++level) {
      struct pan_image_slice_layout *src_slice =
         &prsrc->plane.layout.slices[level];
      struct pan_image_slice_layout *dst_slice =
         &prsrc->afbcp->plane.layout.slices[level];
      unsigned nr_blocks_total =
         pan_afbc_stride_blocks(
            format, modifier, src_slice->afbc.header.row_stride_B) *
         pan_afbc_height_blocks(
            format, modifier, u_minify(prsrc->image.props.extent_px.height, level));
      uint32_t body_offset_B = pan_afbc_body_offset(
         dev->arch, modifier, src_slice->afbc.header.surface_size_B);
      struct pan_afbc_payload_extent *layout =
         prsrc->afbcp->layout_bo->ptr.cpu +
         prsrc->afbcp->layout_offsets[level];
      uint32_t body_size_B = pan_resource_afbcp_get_payload_layout(
         ctx, prsrc, layout, nr_blocks_total, src_slice->offset_B);

      /* Header layout is exactly the same, only the body is shrunk. */
      unsigned size_B = body_offset_B + body_size_B;
      dst_slice->afbc.header = src_slice->afbc.header;
      dst_slice->afbc.surface_stride_B = size_B;
      dst_slice->size_B = size_B;
      dst_slice->offset_B = total_size;

      /* We can't write to AFBC-packed resource, so there is no reason to
       * keep CRC data around */
      dst_slice->crc.offset_B = 0;
      dst_slice->crc.size_B = 0;

      total_size = ALIGN_POT(total_size + size_B,
                             pan_afbc_header_align(dev->arch, modifier));
   }

   prsrc->afbcp->size = ALIGN_POT(total_size, 4096); // FIXME
}

/* Queue a CS modifier conversion job to pack the current sparse AFBC BO into
 * a new packed BO using the AFBC-P payload layout BO. Returns true on
 * success. */
static bool
pan_resource_afbcp_pack(struct panfrost_context *ctx,
                        struct panfrost_resource *prsrc)
{
   MESA_TRACE_FUNC();

   afbcp_debug(ctx, "AFBC-P prsrc=%p: Pack (reads=%u bo_size=%zu ratio=%.2f)",
               prsrc, prsrc->afbcp->nr_consecutive_reads,
               panfrost_bo_size(prsrc->bo), prsrc->afbcp->ratio);

   struct panfrost_screen *screen = pan_screen(ctx->base.screen);
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   uint64_t modifier = prsrc->modifier & ~AFBC_FORMAT_MOD_SPARSE;
   unsigned last_level = prsrc->base.last_level;

   const char *old_user_label = NULL;
   if (prsrc->bo->label) {
      old_user_label = strstr(prsrc->bo->label, "user_label=");
      if (old_user_label)
         old_user_label += strlen("user_label=");
   }
   char *new_label = panfrost_resource_new_label(
      prsrc, modifier, old_user_label);

   prsrc->afbcp->packed_bo = panfrost_bo_create(
      dev, prsrc->afbcp->size, 0, new_label);
   if (!prsrc->afbcp->packed_bo) {
      mesa_loge("pan_resource_afbcp_pack: failed to create AFBC-P BO");
      free(new_label);
      return false;
   }

   prsrc->afbcp->skip_access_updates = true;

   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
   for (unsigned level = 0; level <= last_level; ++level)
      screen->vtbl.afbc_pack(batch, prsrc, prsrc->afbcp->packed_bo,
                             &prsrc->afbcp->plane.layout.slices[level],
                             prsrc->afbcp->layout_bo,
                             prsrc->afbcp->layout_offsets[level], level);

   prsrc->afbcp->skip_access_updates = false;

   return true;
}

/* Replace the current sparse BO by the newly packed BO. */
static void
pan_resource_afbcp_commit(struct panfrost_context *ctx,
                          struct panfrost_resource *prsrc)
{
   MESA_TRACE_FUNC();

   afbcp_debug(ctx,
               "AFBC-P prsrc=%p: Commit (reads=%u bo_size=%zu ratio=%.2f)",
               prsrc, prsrc->afbcp->nr_consecutive_reads,
               panfrost_bo_size(prsrc->bo), prsrc->afbcp->ratio);

   uint64_t modifier = prsrc->modifier & ~AFBC_FORMAT_MOD_SPARSE;
   assert(!panfrost_is_emulated_mod(modifier));
   prsrc->image.props.modifier = modifier;
   prsrc->modifier = modifier;

   prsrc->plane.layout.array_stride_B = prsrc->afbcp->size;
   prsrc->plane.layout.data_size_B = prsrc->afbcp->size;
   prsrc->plane.base = prsrc->afbcp->packed_bo->ptr.gpu;
   prsrc->image.props.crc = false;
   prsrc->valid.crc = false;

   for (unsigned level = 0; level <= prsrc->base.last_level; ++level)
      prsrc->plane.layout.slices[level] =
         prsrc->afbcp->plane.layout.slices[level];

   if (prsrc->owns_label)
      free((char *)panfrost_bo_replace_label(
              prsrc->bo, "Disposed pre AFBC-P BO", false));
   panfrost_bo_unreference(prsrc->bo);
   prsrc->bo = prsrc->afbcp->packed_bo;
   prsrc->afbcp->packed_bo = NULL;

   pan_resource_afbcp_stop(prsrc);
}

/* Progressively pack AFBC resources. */
void
pan_resource_afbcp_update(struct panfrost_context *ctx,
                          struct panfrost_resource *prsrc,
                          bool write)
{
   struct panfrost_screen *screen = pan_screen(ctx->base.screen);

   if (prsrc->afbcp->skip_access_updates)
      return;

   if (write) {
      pan_resource_afbcp_restart(prsrc);
      return;
   }

   if (++prsrc->afbcp->nr_consecutive_reads < screen->afbcp_reads_threshold)
      return;

   /* Don't bother if there's a write in the queue. */
   if (panfrost_any_batch_writes_rsrc(ctx, prsrc))
      return;

   /* 1st async AFBC-P step: get payload sizes. */
   if (!prsrc->afbcp->layout_bo) {
      if (!panfrost_bo_wait(prsrc->bo, 0, false))
         return;
      if (!pan_resource_afbcp_get_payload_sizes(ctx, prsrc))
         goto stop_packing;
      return;
   }

   /* 2nd async AFBC-P step: get payload offsets. */
   if (prsrc->afbcp->size == 0) {
      if (!panfrost_bo_wait(prsrc->afbcp->layout_bo, 0, false))
         return;
      pan_resource_afbcp_get_payload_offsets(ctx, prsrc);
      return;
   }

   /* Validate compression ratio. */
   if (prsrc->afbcp->ratio == 0.0f) {
      prsrc->afbcp->ratio =
         (float)panfrost_bo_size(prsrc->bo) / prsrc->afbcp->size;
      if (100.0f / prsrc->afbcp->ratio > screen->max_afbc_packing_ratio)
         goto stop_packing;
   }

   /* 3rd async AFBC-P step: pack. */
   if (!prsrc->afbcp->packed_bo) {
      if (!panfrost_bo_wait(prsrc->bo, 0, false) ||
          !panfrost_bo_wait(prsrc->afbcp->layout_bo, 0, false))
         return;
      if (!pan_resource_afbcp_pack(ctx, prsrc))
         goto stop_packing;
      return;
   }

   /* 4th async AFBC-P step: commit. */
   if (!panfrost_bo_wait(prsrc->afbcp->packed_bo, 0, false))
      return;
   pan_resource_afbcp_commit(ctx, prsrc);
   return;

 stop_packing:
   pan_resource_afbcp_stop(prsrc);
}

static void
panfrost_ptr_unmap(struct pipe_context *pctx, struct pipe_transfer *transfer)
{
   MESA_TRACE_FUNC();

   /* Gallium expects writeback here, so we tile */

   struct panfrost_context *ctx = pan_context(pctx);
   struct pipe_screen *screen = ctx->base.screen;
   struct panfrost_transfer *trans = pan_transfer(transfer);
   struct panfrost_resource *prsrc =
      (struct panfrost_resource *)transfer->resource;
   struct panfrost_device *dev = pan_device(pctx->screen);

   if (transfer->usage & PIPE_MAP_WRITE)
      prsrc->valid.crc = false;

   /* AFBC/AFRC will use a staging resource. `initialized` will be set when
    * the fragment job is created; this is deferred to prevent useless surface
    * reloads that can cascade into DATA_INVALID_FAULTs due to reading
    * malformed AFBC/AFRC data if uninitialized */

   if (trans->staging.rsrc) {
      if (transfer->usage & PIPE_MAP_WRITE) {
         if (panfrost_should_linear_convert(ctx, prsrc, transfer)) {

            if (prsrc->owns_label) {
               char *old_label = (char *)panfrost_bo_replace_label(prsrc->bo, "Discarded ptr-unmap BO", false);
               free(old_label);
            }
            panfrost_bo_unreference(prsrc->bo);
            pan_resource_afbcp_stop(prsrc);

            panfrost_resource_setup(screen, prsrc, DRM_FORMAT_MOD_LINEAR,
                                    prsrc->image.props.format, 0);

            prsrc->bo = pan_resource(trans->staging.rsrc)->bo;
            prsrc->plane.base = prsrc->bo->ptr.gpu;
            panfrost_bo_reference(prsrc->bo);

            prsrc->owns_label = pan_resource(trans->staging.rsrc)->owns_label;
            pan_resource(trans->staging.rsrc)->owns_label = false;
         } else {
            bool discard = panfrost_can_discard(&prsrc->base, &transfer->box,
                                                transfer->usage);
            pan_legalize_format(ctx, prsrc, prsrc->image.props.format, true,
                                discard);
            pan_blit_from_staging(pctx, trans);
            panfrost_flush_batches_accessing_rsrc(
               ctx, pan_resource(trans->staging.rsrc),
               "AFBC write staging blit");

            if (pan_screen(pctx->screen)->force_afbc_packing) {
               if (panfrost_should_pack_afbc(dev, prsrc))
                  pan_resource_afbcp_restart(prsrc);
            }
         }
      }

      pipe_resource_reference(&trans->staging.rsrc, NULL);
   }

   /* Tiling will occur in software from a staging cpu buffer */
   if (trans->map) {
      struct panfrost_bo *bo = prsrc->bo;

      if (transfer->usage & PIPE_MAP_WRITE) {
         BITSET_SET(prsrc->valid.data, transfer->level);

         if (prsrc->modifier ==
             DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
            if (panfrost_should_linear_convert(ctx, prsrc, transfer)) {
               pan_resource_afbcp_stop(prsrc);
               panfrost_resource_setup(screen, prsrc, DRM_FORMAT_MOD_LINEAR,
                                       prsrc->image.props.format, 0);

               /* converting the resource from tiled to linear and back
                * shouldn't increase memory usage...
                */
               assert(prsrc->plane.layout.data_size_B <= panfrost_bo_size(bo));

               util_copy_rect(
                  bo->ptr.cpu + prsrc->plane.layout.slices[0].offset_B,
                  prsrc->base.format,
                  prsrc->plane.layout.slices[0].tiled_or_linear.row_stride_B, 0,
                  0, transfer->box.width, transfer->box.height, trans->map,
                  transfer->stride, 0, 0);
            } else {
               panfrost_store_tiled_images(trans, prsrc);
            }
         }
      }
   }

   util_range_add(&prsrc->base, &prsrc->valid_buffer_range, transfer->box.x,
                  transfer->box.x + transfer->box.width);

   if (transfer->usage & PIPE_MAP_WRITE) {
      pan_minmax_cache_invalidate(prsrc->index_cache,
                                  util_format_get_blocksize(prsrc->base.format),
                                  transfer->box.x, transfer->box.width);
   }

   /* Derefence the resource */
   pipe_resource_reference(&transfer->resource, NULL);

   /* Transfer itself is RALLOCed at the moment */
   ralloc_free(transfer);
}

static void
panfrost_ptr_flush_region(struct pipe_context *pctx,
                          struct pipe_transfer *transfer,
                          const struct pipe_box *box)
{
   struct panfrost_resource *rsc = pan_resource(transfer->resource);

   if (transfer->resource->target == PIPE_BUFFER) {
      util_range_add(&rsc->base, &rsc->valid_buffer_range,
                     transfer->box.x + box->x,
                     transfer->box.x + box->x + box->width);
   } else {
      BITSET_SET(rsc->valid.data, transfer->level);
   }
}

static void
panfrost_invalidate_resource(struct pipe_context *pctx,
                             struct pipe_resource *prsrc)
{
   struct panfrost_context *ctx = pan_context(pctx);
   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
   struct panfrost_resource *rsrc = pan_resource(prsrc);

   if (!batch) {
      mesa_loge("panfrost_invalidate_resource failed");
      return;
   }

   rsrc->constant_stencil = true;

   /* Handle the glInvalidateFramebuffer case */
   if (batch->key.zsbuf.texture == prsrc)
      batch->resolve &= ~PIPE_CLEAR_DEPTHSTENCIL;

   for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
      struct pipe_surface *surf = &batch->key.cbufs[i];

      if (surf->texture == prsrc)
         batch->resolve &= ~(PIPE_CLEAR_COLOR0 << i);
   }
}

static enum pipe_format
panfrost_resource_get_internal_format(struct pipe_resource *rsrc)
{
   struct panfrost_resource *prsrc = (struct panfrost_resource *)rsrc;
   return prsrc->image.props.format;
}

void
panfrost_set_image_view_planes(struct pan_image_view *iview,
                               struct pipe_resource *texture)
{
   struct panfrost_resource *prsrc_plane = (struct panfrost_resource *)texture;
   unsigned view_nplanes = util_format_get_num_planes(iview->format);
   struct pan_image_plane_ref pref = {
      .image = &prsrc_plane->image,
      .plane_idx = pan_resource_plane_index(prsrc_plane),
   };

   if (view_nplanes > 1) {
      assert(pref.plane_idx == 0);
      assert(view_nplanes == util_format_get_num_planes(prsrc_plane->image.props.format));
      for (int i = 0; i < view_nplanes; i++) {
         iview->planes[i] = pref;
         pref.plane_idx++;
         prsrc_plane = (struct panfrost_resource *)prsrc_plane->base.next;
      }
   } else {
      assert(pref.plane_idx <
             util_format_get_num_planes(prsrc_plane->image.props.format));
      iview->planes[0] = pref;
   }
}

static bool
panfrost_generate_mipmap(struct pipe_context *pctx, struct pipe_resource *prsrc,
                         enum pipe_format format, unsigned base_level,
                         unsigned last_level, unsigned first_layer,
                         unsigned last_layer)
{
   struct panfrost_resource *rsrc = pan_resource(prsrc);

   perf_debug(pan_context(pctx), "Unoptimized mipmap generation");

   /* Generating a mipmap invalidates the written levels, so make that
    * explicit so we don't try to wallpaper them back and end up with
    * u_blitter recursion */

   assert(rsrc->bo);
   for (unsigned l = base_level + 1; l <= last_level; ++l)
      BITSET_CLEAR(rsrc->valid.data, l);

   /* Beyond that, we just delegate the hard stuff. */

   bool blit_res =
      util_gen_mipmap(pctx, prsrc, format, base_level, last_level, first_layer,
                      last_layer, PIPE_TEX_FILTER_LINEAR);

   return blit_res;
}

static void
panfrost_resource_set_stencil(struct pipe_resource *prsrc,
                              struct pipe_resource *stencil)
{
   pan_resource(prsrc)->separate_stencil = pan_resource(stencil);
}

static struct pipe_resource *
panfrost_resource_get_stencil(struct pipe_resource *prsrc)
{
   if (!pan_resource(prsrc)->separate_stencil)
      return NULL;

   return &pan_resource(prsrc)->separate_stencil->base;
}

static const struct u_transfer_vtbl transfer_vtbl = {
   .resource_create = panfrost_resource_create,
   .resource_destroy = panfrost_resource_destroy,
   .transfer_map = panfrost_ptr_map,
   .transfer_unmap = panfrost_ptr_unmap,
   .transfer_flush_region = panfrost_ptr_flush_region,
   .get_internal_format = panfrost_resource_get_internal_format,
   .set_stencil = panfrost_resource_set_stencil,
   .get_stencil = panfrost_resource_get_stencil,
};

void
panfrost_resource_screen_init(struct pipe_screen *pscreen)
{
   pscreen->can_create_resource = panfrost_can_create_resource;
   pscreen->resource_create_with_modifiers =
      panfrost_resource_create_with_modifiers;
   pscreen->resource_create = u_transfer_helper_resource_create;
   pscreen->resource_destroy = u_transfer_helper_resource_destroy;
   pscreen->resource_from_handle = panfrost_resource_from_handle;
   pscreen->resource_get_handle = panfrost_resource_get_handle;
   pscreen->set_resource_label = panfrost_set_resource_label;
   pscreen->resource_get_param = panfrost_resource_get_param;
   pscreen->transfer_helper = u_transfer_helper_create(
      &transfer_vtbl,
      U_TRANSFER_HELPER_SEPARATE_Z32S8 | U_TRANSFER_HELPER_MSAA_MAP);
}
void
panfrost_resource_screen_destroy(struct pipe_screen *pscreen)
{
   u_transfer_helper_destroy(pscreen->transfer_helper);
}

void
panfrost_resource_context_init(struct pipe_context *pctx)
{
   pctx->buffer_map = u_transfer_helper_transfer_map;
   pctx->buffer_unmap = u_transfer_helper_transfer_unmap;
   pctx->texture_map = u_transfer_helper_transfer_map;
   pctx->texture_unmap = u_transfer_helper_transfer_unmap;
   pctx->resource_copy_region = util_resource_copy_region;
   pctx->blit = panfrost_blit;
   pctx->generate_mipmap = panfrost_generate_mipmap;
   pctx->flush_resource = panfrost_flush_resource;
   pctx->invalidate_resource = panfrost_invalidate_resource;
   pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
   pctx->buffer_subdata = u_default_buffer_subdata;
   pctx->texture_subdata = u_default_texture_subdata;
   pctx->clear_buffer = u_default_clear_buffer;
   pctx->clear_render_target = panfrost_clear_render_target;
   pctx->clear_depth_stencil = panfrost_clear_depth_stencil;
}
