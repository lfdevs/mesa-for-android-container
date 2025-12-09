/**************************************************************************
 *
 * Copyright 2011 Christian König.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <assert.h>

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_sampler.h"
#include "util/u_memory.h"

#include "vl_video_buffer.h"

const unsigned const_resource_plane_order_YUV[3] = {
   0,
   1,
   2
};

const unsigned const_resource_plane_order_YVU[3] = {
   0,
   2,
   1
};

static enum pipe_format
vl_get_plane_format(enum pipe_format format, unsigned plane)
{
   enum pipe_format fmt = util_format_get_plane_format(format, plane);

   switch (fmt) {
   case PIPE_FORMAT_X6R10_UNORM:
   case PIPE_FORMAT_X4R12_UNORM:
      return PIPE_FORMAT_R16_UNORM;
   case PIPE_FORMAT_X6R10X6G10_UNORM:
   case PIPE_FORMAT_X4R12X4G12_UNORM:
      return PIPE_FORMAT_R16G16_UNORM;
   case PIPE_FORMAT_YUYV:
      return PIPE_FORMAT_R8G8_R8B8_UNORM;
   case PIPE_FORMAT_UYVY:
      return PIPE_FORMAT_G8R8_B8R8_UNORM;
   default:
      return fmt;
   }
}

void
vl_get_video_buffer_formats(struct pipe_screen *screen, enum pipe_format format,
                            enum pipe_format out_format[VL_NUM_COMPONENTS])
{
   unsigned num_planes = util_format_get_num_planes(format);
   unsigned i;

   for (i = 0; i < num_planes; i++)
      out_format[i] = vl_get_plane_format(format, i);
   for (; i < VL_NUM_COMPONENTS; i++)
      out_format[i] = PIPE_FORMAT_NONE;
}

const unsigned *
vl_video_buffer_plane_order(enum pipe_format format)
{
   if (util_format_get_num_planes(format) == 1)
      return const_resource_plane_order_YUV;

   switch(format) {
   case PIPE_FORMAT_YV12:
      return const_resource_plane_order_YVU;

   case PIPE_FORMAT_NV12:
   case PIPE_FORMAT_NV21:
   case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
   case PIPE_FORMAT_Y8_U8_V8_440_UNORM:
   case PIPE_FORMAT_P010:
   case PIPE_FORMAT_P012:
   case PIPE_FORMAT_P016:
   case PIPE_FORMAT_IYUV:
      return const_resource_plane_order_YUV;

   default:
      assert(0);
      return NULL;
   }
}

static enum pipe_format
vl_video_buffer_surface_format(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   /* a subsampled formats can't work as surface use RGBA instead */
   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED)
      return PIPE_FORMAT_R8G8B8A8_UNORM;

   return format;
}

bool
vl_video_buffer_is_format_supported(struct pipe_screen *screen,
                                    enum pipe_format format,
                                    enum pipe_video_profile profile,
                                    enum pipe_video_entrypoint entrypoint)
{
   enum pipe_format resource_formats[VL_NUM_COMPONENTS];
   unsigned i;

   assert(format != PIPE_FORMAT_NONE);

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING && format == PIPE_FORMAT_R8_G8_B8_UNORM)
      return false;

   vl_get_video_buffer_formats(screen, format, resource_formats);

   for (i = 0; i < VL_NUM_COMPONENTS; ++i) {
      enum pipe_format fmt = resource_formats[i];

      if (fmt == PIPE_FORMAT_NONE)
         continue;

      /* we at least need to sample from it */
      if (!screen->is_format_supported(screen, fmt, PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW))
         return false;

      fmt = vl_video_buffer_surface_format(fmt);
      if (!screen->is_format_supported(screen, fmt, PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_RENDER_TARGET))
         return false;
   }

   return true;
}

unsigned
vl_video_buffer_max_size(struct pipe_screen *screen)
{
   return screen->caps.max_texture_2d_size;
}

void
vl_video_buffer_set_associated_data(struct pipe_video_buffer *vbuf,
                                    struct pipe_video_codec *vcodec,
                                    void *associated_data,
                                    void (*destroy_associated_data)(void *))
{
   vbuf->codec = vcodec;

   if (vbuf->associated_data == associated_data)
      return;

   if (vbuf->associated_data)
      vbuf->destroy_associated_data(vbuf->associated_data);

   vbuf->associated_data = associated_data;
   vbuf->destroy_associated_data = destroy_associated_data;
}

void *
vl_video_buffer_get_associated_data(struct pipe_video_buffer *vbuf,
                                    struct pipe_video_codec *vcodec)
{
   if (vbuf->codec == vcodec)
      return vbuf->associated_data;
   else
      return NULL;
}

void
vl_video_buffer_template(struct pipe_resource *templ,
                         const struct pipe_video_buffer *tmpl,
                         enum pipe_format resource_format,
                         unsigned depth, unsigned array_size,
                         unsigned usage, unsigned plane,
                         enum pipe_video_chroma_format chroma_format)
{
   unsigned height = tmpl->height;

   memset(templ, 0, sizeof(*templ));
   if (depth > 1)
      templ->target = PIPE_TEXTURE_3D;
   else if (array_size > 1)
      templ->target = PIPE_TEXTURE_2D_ARRAY;
   else
      templ->target = PIPE_TEXTURE_2D;
   templ->format = resource_format;
   templ->width0 = tmpl->width;
   templ->depth0 = depth;
   templ->array_size = array_size;
   templ->bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET | tmpl->bind;
   templ->usage = usage;
   templ->flags = tmpl->flags;

   vl_video_buffer_adjust_size(&templ->width0, &height, plane,
                               chroma_format, false);
   templ->height0 = height;
}

void
vl_video_buffer_destroy(struct pipe_video_buffer *buffer)
{
   struct vl_video_buffer *buf = (struct vl_video_buffer *)buffer;
   unsigned i;

   assert(buf);

   for (i = 0; i < VL_NUM_COMPONENTS; ++i) {
      buf->base.context->sampler_view_release(buf->base.context, buf->sampler_view_planes[i]);
      if (i < buf->num_sampler_view_components)
         buf->base.context->sampler_view_release(buf->base.context, buf->sampler_view_components[i]);
      pipe_resource_reference(&buf->resources[i], NULL);
   }

   vl_video_buffer_set_associated_data(buffer, NULL, NULL, NULL);

   FREE(buffer);
}

static void
vl_video_buffer_resources(struct pipe_video_buffer *buffer,
                          struct pipe_resource **resources)
{
   struct vl_video_buffer *buf = (struct vl_video_buffer *)buffer;
   unsigned num_planes = util_format_get_num_planes(buffer->buffer_format);
   unsigned i;

   assert(buf);

   for (i = 0; i < num_planes; ++i) {
      resources[i] = buf->resources[i];
   }
}

static struct pipe_sampler_view **
vl_video_buffer_sampler_view_planes(struct pipe_video_buffer *buffer)
{
   struct vl_video_buffer *buf = (struct vl_video_buffer *)buffer;
   unsigned num_planes = util_format_get_num_planes(buffer->buffer_format);
   struct pipe_sampler_view sv_templ;
   struct pipe_context *pipe;
   unsigned i;

   assert(buf);

   pipe = buf->base.context;

   for (i = 0; i < num_planes; ++i ) {
      if (!buf->sampler_view_planes[i]) {
         memset(&sv_templ, 0, sizeof(sv_templ));
         u_sampler_view_default_template(&sv_templ, buf->resources[i], buf->resources[i]->format);

         if (util_format_get_nr_components(buf->resources[i]->format) == 1)
            sv_templ.swizzle_r = sv_templ.swizzle_g = sv_templ.swizzle_b = sv_templ.swizzle_a = PIPE_SWIZZLE_X;

         buf->sampler_view_planes[i] = pipe->create_sampler_view(pipe, buf->resources[i], &sv_templ);
         if (!buf->sampler_view_planes[i])
            goto error;
      }
   }

   return buf->sampler_view_planes;

error:
   for (i = 0; i < num_planes; ++i)
      pipe->sampler_view_release(pipe, buf->sampler_view_planes[i]);

   return NULL;
}

static struct pipe_sampler_view **
vl_video_buffer_sampler_view_components(struct pipe_video_buffer *buffer)
{
   struct vl_video_buffer *buf = (struct vl_video_buffer *)buffer;
   struct pipe_sampler_view sv_templ;
   struct pipe_context *pipe;
   enum pipe_format sampler_format[VL_NUM_COMPONENTS];
   const unsigned *plane_order;
   unsigned i, j, component, num_planes;

   assert(buf);

   pipe = buf->base.context;

   vl_get_video_buffer_formats(pipe->screen, buf->base.buffer_format, sampler_format);
   plane_order = vl_video_buffer_plane_order(buf->base.buffer_format);
   num_planes = util_format_get_num_planes(buf->base.buffer_format);

   for (component = 0, i = 0; i < num_planes; ++i) {
      struct pipe_resource *res = buf->resources[plane_order[i]];
      const struct util_format_description *desc = util_format_description(res->format);
      unsigned nr_components = util_format_get_nr_components(res->format);
      if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED)
         nr_components = 3;

      for (j = 0; j < nr_components && component < VL_NUM_COMPONENTS; ++j, ++component) {
         unsigned pipe_swizzle;

         if (buf->sampler_view_components[component])
            continue;

         memset(&sv_templ, 0, sizeof(sv_templ));
         u_sampler_view_default_template(&sv_templ, res, sampler_format[plane_order[i]]);
         pipe_swizzle = (buf->base.buffer_format == PIPE_FORMAT_YUYV || buf->base.buffer_format == PIPE_FORMAT_UYVY) ?
                        (PIPE_SWIZZLE_X + j + 1) % 3 :
                        (PIPE_SWIZZLE_X + j);
         sv_templ.swizzle_r = sv_templ.swizzle_g = sv_templ.swizzle_b = pipe_swizzle;
         sv_templ.swizzle_a = PIPE_SWIZZLE_1;

         buf->sampler_view_components[component] = pipe->create_sampler_view(pipe, res, &sv_templ);
         if (!buf->sampler_view_components[component])
            goto error;
      }
   }

   assert(component != 0);

   for (i = component; i < VL_NUM_COMPONENTS; ++i)
      buf->sampler_view_components[i] = buf->sampler_view_components[component - 1];

   buf->num_sampler_view_components = component;

   return buf->sampler_view_components;

error:
   for (i = 0; i < buf->num_sampler_view_components; ++i)
      pipe->sampler_view_release(pipe, buf->sampler_view_components[i]);

   return NULL;
}

static struct pipe_surface *
vl_video_buffer_get_surfaces(struct pipe_video_buffer *buffer)
{
   struct vl_video_buffer *buf = (struct vl_video_buffer *)buffer;
   return &buf->surfaces[0];
}

static void
vl_video_buffer_surfaces(struct vl_video_buffer *buf)
{
   unsigned i, j, array_size, surf;

   assert(buf);

   array_size = buf->base.interlaced ? 2 : 1;
   for (i = 0, surf = 0; i < VL_NUM_COMPONENTS; ++i) {
      for (j = 0; j < array_size; ++j, ++surf) {
         assert(surf < VL_MAX_SURFACES);

         if (!buf->resources[i]) {
            memset(&buf->surfaces[surf], 0, sizeof(buf->surfaces[0]));
            continue;
         }

         buf->surfaces[surf].texture = buf->resources[i];
         buf->surfaces[surf].format = vl_video_buffer_surface_format(buf->resources[i]->format);
         buf->surfaces[surf].first_layer = buf->surfaces[surf].last_layer = j;
      }
   }
}

struct pipe_video_buffer *
vl_video_buffer_create(struct pipe_context *pipe,
                       const struct pipe_video_buffer *tmpl)
{
   enum pipe_format resource_formats[VL_NUM_COMPONENTS];
   struct pipe_video_buffer templat, *result;
   bool pot_buffers;

   assert(pipe);
   assert(tmpl->width > 0 && tmpl->height > 0);

   pot_buffers = !pipe->screen->get_video_param
   (
      pipe->screen,
      PIPE_VIDEO_PROFILE_UNKNOWN,
      PIPE_VIDEO_ENTRYPOINT_UNKNOWN,
      PIPE_VIDEO_CAP_NPOT_TEXTURES
   );

   vl_get_video_buffer_formats(pipe->screen, tmpl->buffer_format, resource_formats);

   templat = *tmpl;
   templat.width = pot_buffers ? util_next_power_of_two(tmpl->width)
                 : align(tmpl->width, VL_MACROBLOCK_WIDTH);
   templat.height = pot_buffers ? util_next_power_of_two(tmpl->height)
                  : align(tmpl->height, VL_MACROBLOCK_HEIGHT);

   if (tmpl->interlaced)
      templat.height /= 2;

   result = vl_video_buffer_create_ex
   (
      pipe, &templat, resource_formats,
      1, tmpl->interlaced ? 2 : 1, PIPE_USAGE_DEFAULT,
      pipe_format_to_chroma_format(templat.buffer_format)
   );


   if (result && tmpl->interlaced)
      result->height *= 2;

   return result;
}

struct pipe_video_buffer *
vl_video_buffer_create_ex(struct pipe_context *pipe,
                          const struct pipe_video_buffer *tmpl,
                          const enum pipe_format resource_formats[VL_NUM_COMPONENTS],
                          unsigned depth, unsigned array_size, unsigned usage,
                          enum pipe_video_chroma_format chroma_format)
{
   struct pipe_resource res_tmpl;
   struct pipe_resource *resources[VL_NUM_COMPONENTS];
   unsigned i;

   assert(pipe);

   memset(resources, 0, sizeof resources);

   vl_video_buffer_template(&res_tmpl, tmpl, resource_formats[0], depth, array_size,
                            usage, 0, chroma_format);
   resources[0] = pipe->screen->resource_create(pipe->screen, &res_tmpl);
   if (!resources[0])
      goto error;

   if (resource_formats[1] == PIPE_FORMAT_NONE) {
      assert(resource_formats[2] == PIPE_FORMAT_NONE);
      return vl_video_buffer_create_ex2(pipe, tmpl, resources);
   }

   vl_video_buffer_template(&res_tmpl, tmpl, resource_formats[1], depth, array_size,
                            usage, 1, chroma_format);
   resources[1] = pipe->screen->resource_create(pipe->screen, &res_tmpl);
   if (!resources[1])
      goto error;

   if (resource_formats[2] == PIPE_FORMAT_NONE)
      return vl_video_buffer_create_ex2(pipe, tmpl, resources);

   vl_video_buffer_template(&res_tmpl, tmpl, resource_formats[2], depth, array_size,
                            usage, 2, chroma_format);
   resources[2] = pipe->screen->resource_create(pipe->screen, &res_tmpl);
   if (!resources[2])
      goto error;

   return vl_video_buffer_create_ex2(pipe, tmpl, resources);

error:
   for (i = 0; i < VL_NUM_COMPONENTS; ++i)
      pipe_resource_reference(&resources[i], NULL);

   return NULL;
}

struct pipe_video_buffer *
vl_video_buffer_create_ex2(struct pipe_context *pipe,
                           const struct pipe_video_buffer *tmpl,
                           struct pipe_resource *resources[VL_NUM_COMPONENTS])
{
   struct vl_video_buffer *buffer;
   unsigned i, num_planes;

   buffer = CALLOC_STRUCT(vl_video_buffer);
   if (!buffer)
      return NULL;

   num_planes = util_format_get_num_planes(tmpl->buffer_format);

   buffer->base = *tmpl;
   buffer->base.context = pipe;
   buffer->base.destroy = vl_video_buffer_destroy;
   buffer->base.get_resources = vl_video_buffer_resources;
   buffer->base.get_sampler_view_planes = vl_video_buffer_sampler_view_planes;
   buffer->base.get_sampler_view_components = vl_video_buffer_sampler_view_components;
   buffer->base.get_surfaces = vl_video_buffer_get_surfaces;

   for (i = 0; i < num_planes; ++i)
      buffer->resources[i] = resources[i];

   /* Ignore auxiliary planes. */
   for (; i < VL_NUM_COMPONENTS; ++i) {
      struct pipe_resource *res = resources[i];
      pipe_resource_reference(&res, NULL);
   }

   vl_video_buffer_surfaces(buffer);

   return &buffer->base;
}

/* Create pipe_video_buffer by using resource_create with planar formats. */
struct pipe_video_buffer *
vl_video_buffer_create_as_resource(struct pipe_context *pipe,
                                   const struct pipe_video_buffer *tmpl,
                                   const uint64_t *modifiers,
                                   int modifiers_count)
{
   struct pipe_resource templ, *resources[VL_NUM_COMPONENTS] = {0};
   unsigned array_size =  tmpl->interlaced ? 2 : 1;

   memset(&templ, 0, sizeof(templ));
   templ.target = array_size > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
   templ.width0 = align(tmpl->width, VL_MACROBLOCK_WIDTH);
   templ.height0 = align(tmpl->height / array_size, VL_MACROBLOCK_HEIGHT);
   templ.depth0 = 1;
   templ.array_size = array_size;
   templ.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET | tmpl->bind;
   templ.usage = PIPE_USAGE_DEFAULT;
   templ.flags = tmpl->flags;

   if (tmpl->buffer_format == PIPE_FORMAT_YUYV)
      templ.format = PIPE_FORMAT_R8G8_R8B8_UNORM;
   else if (tmpl->buffer_format == PIPE_FORMAT_UYVY)
      templ.format = PIPE_FORMAT_G8R8_B8R8_UNORM;
   else
      templ.format = tmpl->buffer_format;

   if (modifiers)
      resources[0] = pipe->screen->resource_create_with_modifiers(pipe->screen,
                                                                  &templ, modifiers,
                                                                  modifiers_count);
   else
      resources[0] = pipe->screen->resource_create(pipe->screen, &templ);
   if (!resources[0])
      return NULL;

   if (resources[0]->next) {
      pipe_resource_reference(&resources[1], resources[0]->next);
      if (resources[1]->next)
         pipe_resource_reference(&resources[2], resources[1]->next);
   }

   struct pipe_video_buffer vidtemplate = *tmpl;
   vidtemplate.width = templ.width0;
   vidtemplate.height = templ.height0 * array_size;
   vidtemplate.contiguous_planes = true;
   return vl_video_buffer_create_ex2(pipe, &vidtemplate, resources);
}
