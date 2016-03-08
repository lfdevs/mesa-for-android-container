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
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "svga_cmd.h"
#include "svga_context.h"
#include "svga_format.h"
#include "svga_resource_buffer.h"
#include "svga_resource_texture.h"
#include "svga_shader.h"
#include "svga_state.h"
#include "svga_sampler_view.h"


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
                                           unsigned shader)
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

      format = svga_translate_format(ss, sv->base.format,
                                     PIPE_BIND_SAMPLER_VIEW);
      assert(format != SVGA3D_FORMAT_INVALID);

      /* Convert the format to a sampler-friendly format, if needed */
      format = svga_sampler_format(format);

      if (texture->target == PIPE_BUFFER) {
         viewDesc.buffer.firstElement = sv->base.u.buf.first_element;
         viewDesc.buffer.numElements = (sv->base.u.buf.last_element -
                                        sv->base.u.buf.first_element + 1);
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
   unsigned shader;

   if (!svga_have_vgpu10(svga))
      return PIPE_OK;

   for (shader = PIPE_SHADER_VERTEX; shader <= PIPE_SHADER_GEOMETRY; shader++) {
      SVGA3dShaderResourceViewId ids[PIPE_MAX_SAMPLERS];
      struct svga_winsys_surface *surfaces[PIPE_MAX_SAMPLERS];
      unsigned count;
      unsigned nviews;
      unsigned i;

      count = svga->curr.num_sampler_views[shader];
      for (i = 0; i < count; i++) {
         struct svga_pipe_sampler_view *sv =
            svga_pipe_sampler_view(svga->curr.sampler_views[shader][i]);
         struct svga_winsys_surface *surface;

         if (sv) {
            surface = svga_resource_handle(sv->base.texture);

            ret = svga_validate_pipe_sampler_view(svga, sv);
            if (ret != PIPE_OK)
               return ret;

            assert(sv->id != SVGA3D_INVALID_ID);
            ids[i] = sv->id;
         }
         else {
            surface = NULL;
            ids[i] = SVGA3D_INVALID_ID;
         }
         surfaces[i] = surface;
      }

      for (; i < Elements(ids); i++) {
         ids[i] = SVGA3D_INVALID_ID;
         surfaces[i] = NULL;
      }

      if (shader == PIPE_SHADER_FRAGMENT) {
         /* Handle polygon stipple sampler view */
         if (svga->curr.rast->templ.poly_stipple_enable) {
            const unsigned unit = svga->state.hw_draw.fs->pstipple_sampler_unit;
            struct svga_pipe_sampler_view *sv =
               svga->polygon_stipple.sampler_view;

            assert(sv);
            if (!sv) {
               return PIPE_OK;  /* probably out of memory */
            }

            ret = svga_validate_pipe_sampler_view(svga, sv);
            if (ret != PIPE_OK)
               return ret;

            ids[unit] = sv->id;
            surfaces[unit] = svga_resource_handle(sv->base.texture);
            count = MAX2(count, unit+1);
         }
      }

      /* Number of ShaderResources that need to be modified. This includes
       * the one that need to be unbound.
       */
      nviews = MAX2(svga->state.hw_draw.num_sampler_views[shader], count);
      if (nviews > 0) {
         ret = SVGA3D_vgpu10_SetShaderResources(svga->swc,
                                                svga_shader_type(shader),
                                                0, /* startView */
                                                nviews,
                                                ids,
                                                surfaces);
         if (ret != PIPE_OK)
            return ret;
      }

      /* Number of sampler views enabled in the device */
      svga->state.hw_draw.num_sampler_views[shader] = count;
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
   unsigned shader;

   if (!svga_have_vgpu10(svga))
      return PIPE_OK;

   for (shader = PIPE_SHADER_VERTEX; shader <= PIPE_SHADER_GEOMETRY; shader++) {
      const unsigned count = svga->curr.num_samplers[shader];
      SVGA3dSamplerId ids[PIPE_MAX_SAMPLERS];
      unsigned i;

      for (i = 0; i < count; i++) {
         if (svga->curr.sampler[shader][i]) {
            ids[i] = svga->curr.sampler[shader][i]->id;
            assert(ids[i] != SVGA3D_INVALID_ID);
         }
         else {
            ids[i] = SVGA3D_INVALID_ID;
         }
      }

      if (count > 0) {
         if (count != svga->state.hw_draw.num_samplers[shader] ||
             memcmp(ids, svga->state.hw_draw.samplers[shader],
                    count * sizeof(ids[0])) != 0) {
            /* HW state is really changing */
            ret = SVGA3D_vgpu10_SetSamplers(svga->swc,
                                            count,
                                            0,                       /* start */
                                            svga_shader_type(shader), /* type */
                                            ids);
            if (ret != PIPE_OK)
               return ret;
            memcpy(svga->state.hw_draw.samplers[shader], ids,
                   count * sizeof(ids[0]));
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

      ret = SVGA3D_vgpu10_SetSamplers(svga->swc,
                                      1, /* count */
                                      unit, /* start */
                                      SVGA3D_SHADERTYPE_PS,
                                      &sampler->id);
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
