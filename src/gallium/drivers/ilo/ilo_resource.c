/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2013 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "core/ilo_state_vf.h"
#include "core/ilo_state_sol.h"
#include "core/ilo_state_surface.h"

#include "ilo_screen.h"
#include "ilo_format.h"
#include "ilo_resource.h"

/*
 * From the Ivy Bridge PRM, volume 1 part 1, page 105:
 *
 *     "In addition to restrictions on maximum height, width, and depth,
 *      surfaces are also restricted to a maximum size in bytes. This
 *      maximum is 2 GB for all products and all surface types."
 */
static const size_t ilo_max_resource_size = 1u << 31;

static const char *
resource_get_bo_name(const struct pipe_resource *templ)
{
   static const char *target_names[PIPE_MAX_TEXTURE_TYPES] = {
      [PIPE_BUFFER] = "buf",
      [PIPE_TEXTURE_1D] = "tex-1d",
      [PIPE_TEXTURE_2D] = "tex-2d",
      [PIPE_TEXTURE_3D] = "tex-3d",
      [PIPE_TEXTURE_CUBE] = "tex-cube",
      [PIPE_TEXTURE_RECT] = "tex-rect",
      [PIPE_TEXTURE_1D_ARRAY] = "tex-1d-array",
      [PIPE_TEXTURE_2D_ARRAY] = "tex-2d-array",
      [PIPE_TEXTURE_CUBE_ARRAY] = "tex-cube-array",
   };
   const char *name = target_names[templ->target];

   if (templ->target == PIPE_BUFFER) {
      switch (templ->bind) {
      case PIPE_BIND_VERTEX_BUFFER:
         name = "buf-vb";
         break;
      case PIPE_BIND_INDEX_BUFFER:
         name = "buf-ib";
         break;
      case PIPE_BIND_CONSTANT_BUFFER:
         name = "buf-cb";
         break;
      case PIPE_BIND_STREAM_OUTPUT:
         name = "buf-so";
         break;
      default:
         break;
      }
   }

   return name;
}

static bool
resource_get_cpu_init(const struct pipe_resource *templ)
{
   return (templ->bind & (PIPE_BIND_DEPTH_STENCIL |
                          PIPE_BIND_RENDER_TARGET |
                          PIPE_BIND_STREAM_OUTPUT)) ? false : true;
}

static enum gen_surface_type
get_surface_type(enum pipe_texture_target target)
{
   switch (target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return GEN6_SURFTYPE_1D;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      return GEN6_SURFTYPE_2D;
   case PIPE_TEXTURE_3D:
      return GEN6_SURFTYPE_3D;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return GEN6_SURFTYPE_CUBE;
   default:
      assert(!"unknown texture target");
      return GEN6_SURFTYPE_NULL;
   }
}

static enum pipe_format
resource_get_image_format(const struct pipe_resource *templ,
                          const struct ilo_dev *dev,
                          bool *separate_stencil_ret)
{
   enum pipe_format format = templ->format;
   bool separate_stencil;

   /* silently promote ETC1 */
   if (templ->format == PIPE_FORMAT_ETC1_RGB8)
      format = PIPE_FORMAT_R8G8B8X8_UNORM;

   /* separate stencil buffers */
   separate_stencil = false;
   if ((templ->bind & PIPE_BIND_DEPTH_STENCIL) &&
       util_format_is_depth_and_stencil(templ->format)) {
      switch (templ->format) {
      case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
         /* Gen6 requires HiZ to be available for all levels */
         if (ilo_dev_gen(dev) >= ILO_GEN(7) || templ->last_level == 0) {
            format = PIPE_FORMAT_Z32_FLOAT;
            separate_stencil = true;
         }
         break;
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
         format = PIPE_FORMAT_Z24X8_UNORM;
         separate_stencil = true;
         break;
      default:
         break;
      }
   }

   if (separate_stencil_ret)
      *separate_stencil_ret = separate_stencil;

   return format;
}

static inline enum gen_surface_format
pipe_to_surface_format(const struct ilo_dev *dev, enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return GEN6_FORMAT_R32_FLOAT_X8X24_TYPELESS;
   case PIPE_FORMAT_Z32_FLOAT:
      return GEN6_FORMAT_R32_FLOAT;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z24X8_UNORM:
      return GEN6_FORMAT_R24_UNORM_X8_TYPELESS;
   case PIPE_FORMAT_Z16_UNORM:
      return GEN6_FORMAT_R16_UNORM;
   case PIPE_FORMAT_S8_UINT:
      return GEN6_FORMAT_R8_UINT;
   default:
      return ilo_format_translate_color(dev, format);
   }
}

static void
resource_get_image_info(const struct pipe_resource *templ,
                        const struct ilo_dev *dev,
                        enum pipe_format image_format,
                        struct ilo_image_info *info)
{
   memset(info, 0, sizeof(*info));

   info->type = get_surface_type(templ->target);

   info->format = pipe_to_surface_format(dev, image_format);
   info->interleaved_stencil = util_format_is_depth_and_stencil(image_format);
   info->is_integer = util_format_is_pure_integer(image_format);
   info->compressed = util_format_is_compressed(image_format);
   info->block_width = util_format_get_blockwidth(image_format);
   info->block_height = util_format_get_blockheight(image_format);
   info->block_size = util_format_get_blocksize(image_format);

   info->width = templ->width0;
   info->height = templ->height0;
   info->depth = templ->depth0;
   info->array_size = templ->array_size;
   info->level_count = templ->last_level + 1;
   info->sample_count = (templ->nr_samples) ? templ->nr_samples : 1;

   info->aux_disable = (templ->usage == PIPE_USAGE_STAGING);

   if (templ->bind & PIPE_BIND_LINEAR)
      info->valid_tilings = 1 << GEN6_TILING_NONE;

   /*
    * Tiled images must be mapped via GTT to get a linear view.  Prefer linear
    * images when the image size is greater than one-fourth of the mappable
    * aperture.
    */
   if (templ->usage == PIPE_USAGE_STAGING)
      info->prefer_linear_threshold = dev->aperture_mappable / 4;

   info->bind_surface_sampler = (templ->bind & PIPE_BIND_SAMPLER_VIEW);
   info->bind_surface_dp_render = (templ->bind & PIPE_BIND_RENDER_TARGET);
   info->bind_surface_dp_typed = (templ->bind &
         (PIPE_BIND_SHADER_IMAGE | PIPE_BIND_COMPUTE_RESOURCE));
   info->bind_zs = (templ->bind & PIPE_BIND_DEPTH_STENCIL);
   info->bind_scanout = (templ->bind & PIPE_BIND_SCANOUT);
   info->bind_cursor = (templ->bind & PIPE_BIND_CURSOR);
}

static enum gen_surface_tiling
winsys_to_surface_tiling(enum intel_tiling_mode tiling)
{
   switch (tiling) {
   case INTEL_TILING_NONE:
      return GEN6_TILING_NONE;
   case INTEL_TILING_X:
      return GEN6_TILING_X;
   case INTEL_TILING_Y:
      return GEN6_TILING_Y;
   default:
      assert(!"unknown tiling");
      return GEN6_TILING_NONE;
   }
}

static inline enum intel_tiling_mode
surface_to_winsys_tiling(enum gen_surface_tiling tiling)
{
   switch (tiling) {
   case GEN6_TILING_NONE:
      return INTEL_TILING_NONE;
   case GEN6_TILING_X:
      return INTEL_TILING_X;
   case GEN6_TILING_Y:
      return INTEL_TILING_Y;
   default:
      assert(!"unknown tiling");
      return GEN6_TILING_NONE;
   }
}

static void
tex_free_slices(struct ilo_texture *tex)
{
   FREE(tex->slices[0]);
}

static bool
tex_alloc_slices(struct ilo_texture *tex)
{
   const struct pipe_resource *templ = &tex->base;
   struct ilo_texture_slice *slices;
   int depth, lv;

   /* sum the depths of all levels */
   depth = 0;
   for (lv = 0; lv <= templ->last_level; lv++)
      depth += u_minify(templ->depth0, lv);

   /*
    * There are (depth * tex->base.array_size) slices in total.  Either depth
    * is one (non-3D) or templ->array_size is one (non-array), but it does
    * not matter.
    */
   slices = CALLOC(depth * templ->array_size, sizeof(*slices));
   if (!slices)
      return false;

   tex->slices[0] = slices;

   /* point to the respective positions in the buffer */
   for (lv = 1; lv <= templ->last_level; lv++) {
      tex->slices[lv] = tex->slices[lv - 1] +
         u_minify(templ->depth0, lv - 1) * templ->array_size;
   }

   return true;
}

static bool
tex_create_bo(struct ilo_texture *tex)
{
   struct ilo_screen *is = ilo_screen(tex->base.screen);
   const char *name = resource_get_bo_name(&tex->base);
   const bool cpu_init = resource_get_cpu_init(&tex->base);
   struct intel_bo *bo;

   bo = intel_winsys_alloc_bo(is->dev.winsys, name,
         tex->image.bo_stride * tex->image.bo_height, cpu_init);

   /* set the tiling for transfer and export */
   if (bo && (tex->image.tiling == GEN6_TILING_X ||
              tex->image.tiling == GEN6_TILING_Y)) {
      const enum intel_tiling_mode tiling =
         surface_to_winsys_tiling(tex->image.tiling);

      if (intel_bo_set_tiling(bo, tiling, tex->image.bo_stride)) {
         intel_bo_unref(bo);
         bo = NULL;
      }
   }
   if (!bo)
      return false;

   intel_bo_unref(tex->vma.bo);
   ilo_vma_set_bo(&tex->vma, &is->dev, bo, 0);

   return true;
}

static bool
tex_create_separate_stencil(struct ilo_texture *tex)
{
   struct pipe_resource templ = tex->base;
   struct pipe_resource *s8;

   /*
    * Unless PIPE_BIND_DEPTH_STENCIL is set, the resource may have other
    * tilings.  But that should be fine since it will never be bound as the
    * stencil buffer, and our transfer code can handle all tilings.
    */
   templ.format = PIPE_FORMAT_S8_UINT;

   /* no stencil texturing */
   templ.bind &= ~PIPE_BIND_SAMPLER_VIEW;

   s8 = tex->base.screen->resource_create(tex->base.screen, &templ);
   if (!s8)
      return false;

   tex->separate_s8 = ilo_texture(s8);

   assert(tex->separate_s8->image_format == PIPE_FORMAT_S8_UINT);

   return true;
}

static bool
tex_create_hiz(struct ilo_texture *tex)
{
   const struct pipe_resource *templ = &tex->base;
   const uint32_t size = tex->image.aux.bo_stride * tex->image.aux.bo_height;
   struct ilo_screen *is = ilo_screen(tex->base.screen);
   struct intel_bo *bo;

   bo = intel_winsys_alloc_bo(is->dev.winsys, "hiz texture", size, false);
   if (!bo)
      return false;

   ilo_vma_init(&tex->aux_vma, &is->dev, size, 4096);
   ilo_vma_set_bo(&tex->aux_vma, &is->dev, bo, 0);

   if (tex->imported) {
      unsigned lv;

      for (lv = 0; lv <= templ->last_level; lv++) {
         if (tex->image.aux.enables & (1 << lv)) {
            const unsigned num_slices = (templ->target == PIPE_TEXTURE_3D) ?
               u_minify(templ->depth0, lv) : templ->array_size;
            /* this will trigger HiZ resolves */
            const unsigned flags = ILO_TEXTURE_CPU_WRITE;

            ilo_texture_set_slice_flags(tex, lv, 0, num_slices, flags, flags);
         }
      }
   }

   return true;
}

static bool
tex_create_mcs(struct ilo_texture *tex)
{
   const uint32_t size = tex->image.aux.bo_stride * tex->image.aux.bo_height;
   struct ilo_screen *is = ilo_screen(tex->base.screen);
   struct intel_bo *bo;

   assert(tex->image.aux.enables == (1 << (tex->base.last_level + 1)) - 1);

   bo = intel_winsys_alloc_bo(is->dev.winsys, "mcs texture", size, false);
   if (!bo)
      return false;

   ilo_vma_init(&tex->aux_vma, &is->dev, size, 4096);
   ilo_vma_set_bo(&tex->aux_vma, &is->dev, bo, 0);

   return true;
}

static void
tex_destroy(struct ilo_texture *tex)
{
   if (tex->separate_s8)
      tex_destroy(tex->separate_s8);

   intel_bo_unref(tex->vma.bo);
   intel_bo_unref(tex->aux_vma.bo);

   tex_free_slices(tex);
   FREE(tex);
}

static bool
tex_alloc_bos(struct ilo_texture *tex)
{
   if (!tex->imported && !tex_create_bo(tex))
      return false;

   switch (tex->image.aux.type) {
   case ILO_IMAGE_AUX_HIZ:
      if (!tex_create_hiz(tex))
         return false;
      break;
   case ILO_IMAGE_AUX_MCS:
      if (!tex_create_mcs(tex))
         return false;
      break;
   default:
      break;
   }

   return true;
}

static struct intel_bo *
tex_import_handle(struct ilo_texture *tex,
                  const struct winsys_handle *handle,
                  struct ilo_image_info *info)
{
   struct ilo_screen *is = ilo_screen(tex->base.screen);
   const struct pipe_resource *templ = &tex->base;
   const char *name = resource_get_bo_name(&tex->base);
   enum intel_tiling_mode tiling;
   unsigned long pitch;
   struct intel_bo *bo;

   bo = intel_winsys_import_handle(is->dev.winsys, name, handle,
         tex->image.bo_height, &tiling, &pitch);
   /* modify image info */
   if (bo) {
      const uint8_t valid_tilings = 1 << winsys_to_surface_tiling(tiling);

      if (info->valid_tilings && !(info->valid_tilings & valid_tilings)) {
         intel_bo_unref(bo);
         return NULL;
      }

      info->valid_tilings = valid_tilings;
      info->force_bo_stride = pitch;

      /* assume imported RTs are also scanouts */
      if (!info->bind_scanout)
         info->bind_scanout = (templ->usage & PIPE_BIND_RENDER_TARGET);
   }

   return bo;
}

static bool
tex_init_image(struct ilo_texture *tex,
               const struct winsys_handle *handle,
               bool *separate_stencil)
{
   struct ilo_screen *is = ilo_screen(tex->base.screen);
   const struct pipe_resource *templ = &tex->base;
   struct ilo_image *img = &tex->image;
   struct intel_bo *imported_bo = NULL;
   struct ilo_image_info info;

   tex->image_format = resource_get_image_format(templ,
         &is->dev, separate_stencil);
   resource_get_image_info(templ, &is->dev, tex->image_format, &info);

   if (handle) {
      imported_bo = tex_import_handle(tex, handle, &info);
      if (!imported_bo)
         return false;
   }

   if (!ilo_image_init(img, &is->dev, &info)) {
      intel_bo_unref(imported_bo);
      return false;
   }

   /*
    * HiZ requires 8x4 alignment and some levels might need HiZ disabled.  It
    * is generally fine except on Gen6, where HiZ and separate stencil must be
    * enabled together.  For PIPE_FORMAT_Z24X8_UNORM with separate stencil, we
    * can live with stencil values being interleaved for levels where HiZ is
    * disabled.  But it is not the case for PIPE_FORMAT_Z32_FLOAT with
    * separate stencil.  If HiZ was disabled for a level, we had to change the
    * format to PIPE_FORMAT_Z32_FLOAT_S8X24_UINT for the level and that format
    * had a different bpp.  In other words, HiZ has to be available for all
    * levels.
    */
   if (ilo_dev_gen(&is->dev) == ILO_GEN(6) &&
       templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
       tex->image_format == PIPE_FORMAT_Z32_FLOAT &&
       img->aux.enables != (1 << templ->last_level)) {
      tex->image_format = templ->format;
      info.format = pipe_to_surface_format(&is->dev, tex->image_format);
      info.interleaved_stencil = true;

      memset(img, 0, sizeof(*img));
      if (!ilo_image_init(img, &is->dev, &info)) {
         intel_bo_unref(imported_bo);
         return false;
      }
   }

   if (img->bo_height > ilo_max_resource_size / img->bo_stride ||
       !ilo_vma_init(&tex->vma, &is->dev, img->bo_stride * img->bo_height,
          4096)) {
      intel_bo_unref(imported_bo);
      return false;
   }

   if (imported_bo) {
      ilo_vma_set_bo(&tex->vma, &is->dev, imported_bo, 0);
      tex->imported = true;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_MAP_PERSISTENT) {
      /* require on-the-fly tiling/untiling or format conversion */
      if (img->tiling == GEN8_TILING_W || *separate_stencil ||
          tex->image_format != templ->format)
         return false;
   }

   if (!tex_alloc_slices(tex))
      return false;

   return true;
}

static struct pipe_resource *
tex_create(struct pipe_screen *screen,
           const struct pipe_resource *templ,
           const struct winsys_handle *handle)
{
   struct ilo_texture *tex;
   bool separate_stencil;

   tex = CALLOC_STRUCT(ilo_texture);
   if (!tex)
      return NULL;

   tex->base = *templ;
   tex->base.screen = screen;
   pipe_reference_init(&tex->base.reference, 1);

   if (!tex_init_image(tex, handle, &separate_stencil)) {
      FREE(tex);
      return NULL;
   }

   if (!tex_alloc_bos(tex) ||
       (separate_stencil && !tex_create_separate_stencil(tex))) {
      tex_destroy(tex);
      return NULL;
   }

   return &tex->base;
}

static bool
tex_get_handle(struct ilo_texture *tex, struct winsys_handle *handle)
{
   struct ilo_screen *is = ilo_screen(tex->base.screen);
   enum intel_tiling_mode tiling;
   int err;

   /* must match what tex_create_bo() sets */
   if (tex->image.tiling == GEN8_TILING_W)
      tiling = INTEL_TILING_NONE;
   else
      tiling = surface_to_winsys_tiling(tex->image.tiling);

   err = intel_winsys_export_handle(is->dev.winsys, tex->vma.bo, tiling,
         tex->image.bo_stride, tex->image.bo_height, handle);

   return !err;
}

static bool
buf_create_bo(struct ilo_buffer_resource *buf)
{
   struct ilo_screen *is = ilo_screen(buf->base.screen);
   const char *name = resource_get_bo_name(&buf->base);
   const bool cpu_init = resource_get_cpu_init(&buf->base);
   struct intel_bo *bo;

   bo = intel_winsys_alloc_bo(is->dev.winsys, name, buf->bo_size, cpu_init);
   if (!bo)
      return false;

   intel_bo_unref(buf->vma.bo);
   ilo_vma_set_bo(&buf->vma, &is->dev, bo, 0);

   return true;
}

static void
buf_destroy(struct ilo_buffer_resource *buf)
{
   intel_bo_unref(buf->vma.bo);
   FREE(buf);
}

static struct pipe_resource *
buf_create(struct pipe_screen *screen, const struct pipe_resource *templ)
{
   const struct ilo_screen *is = ilo_screen(screen);
   struct ilo_buffer_resource *buf;
   uint32_t alignment;
   unsigned size;

   buf = CALLOC_STRUCT(ilo_buffer_resource);
   if (!buf)
      return NULL;

   buf->base = *templ;
   buf->base.screen = screen;
   pipe_reference_init(&buf->base.reference, 1);

   size = templ->width0;

   /*
    * As noted in ilo_format_translate(), we treat some 3-component formats as
    * 4-component formats to work around hardware limitations.  Imagine the
    * case where the vertex buffer holds a single PIPE_FORMAT_R16G16B16_FLOAT
    * vertex, and buf->bo_size is 6.  The hardware would fail to fetch it at
    * boundary check because the vertex buffer is expected to hold a
    * PIPE_FORMAT_R16G16B16A16_FLOAT vertex and that takes at least 8 bytes.
    *
    * For the workaround to work, we should add 2 to the bo size.  But that
    * would waste a page when the bo size is already page aligned.  Let's
    * round it to page size for now and revisit this when needed.
    */
   if ((templ->bind & PIPE_BIND_VERTEX_BUFFER) &&
       ilo_dev_gen(&is->dev) < ILO_GEN(7.5))
      size = align(size, 4096);

   if (templ->bind & PIPE_BIND_VERTEX_BUFFER)
      size = ilo_state_vertex_buffer_size(&is->dev, size, &alignment);
   if (templ->bind & PIPE_BIND_INDEX_BUFFER)
      size = ilo_state_index_buffer_size(&is->dev, size, &alignment);
   if (templ->bind & PIPE_BIND_STREAM_OUTPUT)
      size = ilo_state_sol_buffer_size(&is->dev, size, &alignment);

   buf->bo_size = size;
   ilo_vma_init(&buf->vma, &is->dev, buf->bo_size, 4096);

   if (buf->bo_size < templ->width0 || buf->bo_size > ilo_max_resource_size ||
       !buf_create_bo(buf)) {
      FREE(buf);
      return NULL;
   }

   return &buf->base;
}

static boolean
ilo_can_create_resource(struct pipe_screen *screen,
                        const struct pipe_resource *templ)
{
   struct ilo_screen *is = ilo_screen(screen);
   enum pipe_format image_format;
   struct ilo_image_info info;
   struct ilo_image img;

   if (templ->target == PIPE_BUFFER)
      return (templ->width0 <= ilo_max_resource_size);

   image_format = resource_get_image_format(templ, &is->dev, NULL);
   resource_get_image_info(templ, &is->dev, image_format, &info);

   memset(&img, 0, sizeof(img));
   ilo_image_init(&img, &ilo_screen(screen)->dev, &info);

   /* as in tex_init_image() */
   if (ilo_dev_gen(&is->dev) == ILO_GEN(6) &&
       templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
       image_format == PIPE_FORMAT_Z32_FLOAT &&
       img.aux.enables != (1 << templ->last_level)) {
      info.format = pipe_to_surface_format(&is->dev, templ->format);
      info.interleaved_stencil = true;
      memset(&img, 0, sizeof(img));
      ilo_image_init(&img, &ilo_screen(screen)->dev, &info);
   }

   return (img.bo_height <= ilo_max_resource_size / img.bo_stride);
}

static struct pipe_resource *
ilo_resource_create(struct pipe_screen *screen,
                    const struct pipe_resource *templ)
{
   if (templ->target == PIPE_BUFFER)
      return buf_create(screen, templ);
   else
      return tex_create(screen, templ, NULL);
}

static struct pipe_resource *
ilo_resource_from_handle(struct pipe_screen *screen,
                         const struct pipe_resource *templ,
                         struct winsys_handle *handle,
                         unsigned usage)
{
   if (templ->target == PIPE_BUFFER)
      return NULL;
   else
      return tex_create(screen, templ, handle);
}

static boolean
ilo_resource_get_handle(struct pipe_screen *screen,
                        struct pipe_context *ctx,
                        struct pipe_resource *res,
                        struct winsys_handle *handle,
                        unsigned usage)
{
   if (res->target == PIPE_BUFFER)
      return false;
   else
      return tex_get_handle(ilo_texture(res), handle);

}

static void
ilo_resource_destroy(struct pipe_screen *screen,
                     struct pipe_resource *res)
{
   if (res->target == PIPE_BUFFER)
      buf_destroy((struct ilo_buffer_resource *) res);
   else
      tex_destroy(ilo_texture(res));
}

/**
 * Initialize resource-related functions.
 */
void
ilo_init_resource_functions(struct ilo_screen *is)
{
   is->base.can_create_resource = ilo_can_create_resource;
   is->base.resource_create = ilo_resource_create;
   is->base.resource_from_handle = ilo_resource_from_handle;
   is->base.resource_get_handle = ilo_resource_get_handle;
   is->base.resource_destroy = ilo_resource_destroy;
}

bool
ilo_resource_rename_bo(struct pipe_resource *res)
{
   if (res->target == PIPE_BUFFER) {
      return buf_create_bo((struct ilo_buffer_resource *) res);
   } else {
      struct ilo_texture *tex = ilo_texture(res);

      /* an imported texture cannot be renamed */
      if (tex->imported)
         return false;

      return tex_create_bo(tex);
   }
}
