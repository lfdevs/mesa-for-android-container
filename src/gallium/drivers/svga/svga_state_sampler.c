/*
 * Copyright 2013 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


/**
 * VGPU10 sampler and sampler view functions.
 */


#include "pipe/p_defines.h"
#include "util/u_bitmask.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "svga_cmd.h"
#include "svga_context.h"
#include "svga_format.h"
#include "svga_resource_buffer.h"
#include "svga_resource_texture.h"
#include "svga_sampler_view.h"
#include "svga_shader.h"
#include "svga_state.h"
#include "svga_surface.h"


/** Get resource handle for a texture or buffer */
static inline struct svga_winsys_surface *
svga_resource_handle(struct pipe_resource *res)
{
   if (res->target == PIPE_BUFFER) {
      return svga_buffer(res)->handle;
   }
   else {
      return svga_texture(res)->handle;
   }
}


/**
 * This helper function returns TRUE if the specified resource collides with
 * any of the resources bound to any of the currently bound sampler views.
 */
boolean
svga_check_sampler_view_resource_collision(struct svga_context *svga,
                                           struct svga_winsys_surface *res,
                                           enum pipe_shader_type shader)
{
   struct pipe_screen *screen = svga->pipe.screen;
   unsigned i;

   if (svga_screen(screen)->debug.no_surface_view) {
      return FALSE;
   }

   for (i = 0; i < svga->curr.num_sampler_views[shader]; i++) {
      struct svga_pipe_sampler_view *sv =
         svga_pipe_sampler_view(svga->curr.sampler_views[shader][i]);

      if (sv && res == svga_resource_handle(sv->base.texture)) {
         return TRUE;
      }
   }

   return FALSE;
}


/**
 * Check if there are any resources that are both bound to a render target
 * and bound as a shader resource for the given type of shader.
 */
boolean
svga_check_sampler_framebuffer_resource_collision(struct svga_context *svga,
                                                  enum pipe_shader_type shader)
{
   struct svga_surface *surf;
   unsigned i;

   for (i = 0; i < svga->curr.framebuffer.nr_cbufs; i++) {
      surf = svga_surface(svga->curr.framebuffer.cbufs[i]);
      if (surf &&
          svga_check_sampler_view_resource_collision(svga, surf->handle,
                                                     shader)) {
         return TRUE;
      }
   }

   surf = svga_surface(svga->curr.framebuffer.zsbuf);
   if (surf &&
       svga_check_sampler_view_resource_collision(svga, surf->handle, shader)) {
      return TRUE;
   }

   return FALSE;
}


/**
 * Create a DX ShaderResourceSamplerView for the given pipe_sampler_view,
 * if needed.
 */
enum pipe_error
svga_validate_pipe_sampler_view(struct svga_context *svga,
                                struct svga_pipe_sampler_view *sv)
{
   enum pipe_error ret = PIPE_OK;

   if (sv->id == SVGA3D_INVALID_ID) {
      struct svga_screen *ss = svga_screen(svga->pipe.screen);
      struct pipe_resource *texture = sv->base.texture;
      struct svga_winsys_surface *surface = svga_resource_handle(texture);
      SVGA3dSurfaceFormat format;
      SVGA3dResourceType resourceDim;
      SVGA3dShaderResourceViewDesc viewDesc;
      enum pipe_format viewFormat = sv->base.format;

      /* vgpu10 cannot create a BGRX view for a BGRA resource, so force it to
       * create a BGRA view (and vice versa).
       */
      if (viewFormat == PIPE_FORMAT_B8G8R8X8_UNORM &&
          texture->format == PIPE_FORMAT_B8G8R8A8_UNORM) {
         viewFormat = PIPE_FORMAT_B8G8R8A8_UNORM;
      }
      else if (viewFormat == PIPE_FORMAT_B8G8R8A8_UNORM &&
          texture->format == PIPE_FORMAT_B8G8R8X8_UNORM) {
         viewFormat = PIPE_FORMAT_B8G8R8X8_UNORM;
      }

      format = svga_translate_format(ss, viewFormat,
                                     PIPE_BIND_SAMPLER_VIEW);
      assert(format != SVGA3D_FORMAT_INVALID);

      /* Convert the format to a sampler-friendly format, if needed */
      format = svga_sampler_format(format);

      if (texture->target == PIPE_BUFFER) {
         unsigned elem_size = util_format_get_blocksize(sv->base.format);

         viewDesc.buffer.firstElement = sv->base.u.buf.offset / elem_size;
         viewDesc.buffer.numElements = sv->base.u.buf.size / elem_size;
      }
      else {
         viewDesc.tex.mostDetailedMip = sv->base.u.tex.first_level;
         viewDesc.tex.firstArraySlice = sv->base.u.tex.first_layer;
         viewDesc.tex.mipLevels = (sv->base.u.tex.last_level -
                                   sv->base.u.tex.first_level + 1);
      }

      /* arraySize in viewDesc specifies the number of array slices in a
       * texture array. For 3D texture, last_layer in
       * pipe_sampler_view specifies the last slice of the texture
       * which is different from the last slice in a texture array,
       * hence we need to set arraySize to 1 explicitly.
       */
      viewDesc.tex.arraySize =
         (texture->target == PIPE_TEXTURE_3D ||
          texture->target == PIPE_BUFFER) ? 1 :
            (sv->base.u.tex.last_layer - sv->base.u.tex.first_layer + 1);

      switch (texture->target) {
      case PIPE_BUFFER:
         resourceDim = SVGA3D_RESOURCE_BUFFER;
         break;
      case PIPE_TEXTURE_1D:
      case PIPE_TEXTURE_1D_ARRAY:
         resourceDim = SVGA3D_RESOURCE_TEXTURE1D;
         break;
      case PIPE_TEXTURE_RECT:
      case PIPE_TEXTURE_2D:
      case PIPE_TEXTURE_2D_ARRAY:
         resourceDim = SVGA3D_RESOURCE_TEXTURE2D;
         break;
      case PIPE_TEXTURE_3D:
         resourceDim = SVGA3D_RESOURCE_TEXTURE3D;
         break;
      case PIPE_TEXTURE_CUBE:
      case PIPE_TEXTURE_CUBE_ARRAY:
         resourceDim = SVGA3D_RESOURCE_TEXTURECUBE;
         break;

      default:
         assert(!"Unexpected texture type");
         resourceDim = SVGA3D_RESOURCE_TEXTURE2D;
      }

      sv->id = util_bitmask_add(svga->sampler_view_id_bm);

      ret = SVGA3D_vgpu10_DefineShaderResourceView(svga->swc,
                                                   sv->id,
                                                   surface,
                                                   format,
                                                   resourceDim,
                                                   &viewDesc);
      if (ret != PIPE_OK) {
         util_bitmask_clear(svga->sampler_view_id_bm, sv->id);
         sv->id = SVGA3D_INVALID_ID;
      }
   }

   return ret;
}


static enum pipe_error
update_sampler_resources(struct svga_context *svga, unsigned dirty)
{
   enum pipe_error ret = PIPE_OK;
   enum pipe_shader_type shader;

   if (!svga_have_vgpu10(svga))
      return PIPE_OK;

   for (shader = PIPE_SHADER_VERTEX; shader <= PIPE_SHADER_GEOMETRY; shader++) {
      SVGA3dShaderResourceViewId ids[PIPE_MAX_SAMPLERS];
      struct svga_winsys_surface *surfaces[PIPE_MAX_SAMPLERS];
      struct pipe_sampler_view *sampler_views[PIPE_MAX_SAMPLERS];
      unsigned count;
      unsigned nviews;
      unsigned i;

      count = svga->curr.num_sampler_views[shader];
      for (i = 0; i < count; i++) {
         struct svga_pipe_sampler_view *sv =
            svga_pipe_sampler_view(svga->curr.sampler_views[shader][i]);

         if (sv) {
            surfaces[i] = svga_resource_handle(sv->base.texture);

            ret = svga_validate_pipe_sampler_view(svga, sv);
            if (ret != PIPE_OK)
               return ret;

            assert(sv->id != SVGA3D_INVALID_ID);
            ids[i] = sv->id;
            sampler_views[i] = &sv->base;
         }
         else {
            surfaces[i] = NULL;
            ids[i] = SVGA3D_INVALID_ID;
            sampler_views[i] = NULL;
         }
      }

      for (; i < svga->state.hw_draw.num_sampler_views[shader]; i++) {
         ids[i] = SVGA3D_INVALID_ID;
         surfaces[i] = NULL;
         sampler_views[i] = NULL;
      }

      /* Number of ShaderResources that need to be modified. This includes
       * the one that need to be unbound.
       */
      nviews = MAX2(svga->state.hw_draw.num_sampler_views[shader], count);
      if (nviews > 0) {
         if (count != svga->state.hw_draw.num_sampler_views[shader] ||
             memcmp(sampler_views, svga->state.hw_draw.sampler_views[shader],
                    count * sizeof(sampler_views[0])) != 0) {
            ret = SVGA3D_vgpu10_SetShaderResources(svga->swc,
                                                svga_shader_type(shader),
                                                0, /* startView */
                                                nviews,
                                                ids,
                                                surfaces);
            if (ret != PIPE_OK)
               return ret;

            /* Save referenced sampler views in the hw draw state.  */
            svga->state.hw_draw.num_sampler_views[shader] = count;
            for (i = 0; i < nviews; i++) {
               pipe_sampler_view_reference(
                  &svga->state.hw_draw.sampler_views[shader][i],
                  sampler_views[i]);
            }
         }
      }
   }

   /* Handle polygon stipple sampler view */
   if (svga->curr.rast->templ.poly_stipple_enable) {
      const unsigned unit = svga->state.hw_draw.fs->pstipple_sampler_unit;
      struct svga_pipe_sampler_view *sv = svga->polygon_stipple.sampler_view;
      struct svga_winsys_surface *surface;

      assert(sv);
      if (!sv) {
         return PIPE_OK;  /* probably out of memory */
      }

      ret = svga_validate_pipe_sampler_view(svga, sv);
      if (ret != PIPE_OK)
         return ret;

      surface = svga_resource_handle(sv->base.texture);
      ret = SVGA3D_vgpu10_SetShaderResources(
               svga->swc,
               svga_shader_type(PIPE_SHADER_FRAGMENT),
               unit, /* startView */
               1,
               &sv->id,
               &surface);
   }
   return ret;
}


struct svga_tracked_state svga_hw_sampler_bindings = {
   "shader resources emit",
   SVGA_NEW_STIPPLE |
   SVGA_NEW_TEXTURE_BINDING,
   update_sampler_resources
};



static enum pipe_error
update_samplers(struct svga_context *svga, unsigned dirty )
{
   enum pipe_error ret = PIPE_OK;
   enum pipe_shader_type shader;

   if (!svga_have_vgpu10(svga))
      return PIPE_OK;

   for (shader = PIPE_SHADER_VERTEX; shader <= PIPE_SHADER_GEOMETRY; shader++) {
      const unsigned count = svga->curr.num_samplers[shader];
      SVGA3dSamplerId ids[PIPE_MAX_SAMPLERS];
      unsigned i;
      unsigned nsamplers;

      for (i = 0; i < count; i++) {
         if (svga->curr.sampler[shader][i]) {
            ids[i] = svga->curr.sampler[shader][i]->id;
            assert(ids[i] != SVGA3D_INVALID_ID);
         }
         else {
            ids[i] = SVGA3D_INVALID_ID;
         }
      }

      for (; i < svga->state.hw_draw.num_samplers[shader]; i++) {
         ids[i] = SVGA3D_INVALID_ID;
      }

      nsamplers = MAX2(svga->state.hw_draw.num_samplers[shader], count);
      if (nsamplers > 0) {
         if (count != svga->state.hw_draw.num_samplers[shader] ||
             memcmp(ids, svga->state.hw_draw.samplers[shader],
                    count * sizeof(ids[0])) != 0) {
            /* HW state is really changing */
            ret = SVGA3D_vgpu10_SetSamplers(svga->swc,
                                            nsamplers,
                                            0,                       /* start */
                                            svga_shader_type(shader), /* type */
                                            ids);
            if (ret != PIPE_OK)
               return ret;
            memcpy(svga->state.hw_draw.samplers[shader], ids,
                   nsamplers * sizeof(ids[0]));
            svga->state.hw_draw.num_samplers[shader] = count;
         }
      }
   }

   /* Handle polygon stipple sampler texture */
   if (svga->curr.rast->templ.poly_stipple_enable) {
      const unsigned unit = svga->state.hw_draw.fs->pstipple_sampler_unit;
      struct svga_sampler_state *sampler = svga->polygon_stipple.sampler;

      assert(sampler);
      if (!sampler) {
         return PIPE_OK; /* probably out of memory */
      }

      if (svga->state.hw_draw.samplers[PIPE_SHADER_FRAGMENT][unit]
          != sampler->id) {
         ret = SVGA3D_vgpu10_SetSamplers(svga->swc,
                                         1, /* count */
                                         unit, /* start */
                                         SVGA3D_SHADERTYPE_PS,
                                         &sampler->id);
         if (ret != PIPE_OK)
            return ret;

         /* save the polygon stipple sampler in the hw draw state */
         svga->state.hw_draw.samplers[PIPE_SHADER_FRAGMENT][unit] =
            sampler->id;
      }
   }

   return ret;
}


struct svga_tracked_state svga_hw_sampler = {
   "texture sampler emit",
   (SVGA_NEW_SAMPLER |
    SVGA_NEW_STIPPLE |
    SVGA_NEW_TEXTURE_FLAGS),
   update_samplers
};
