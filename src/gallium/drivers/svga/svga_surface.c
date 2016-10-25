/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
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
 *
 **********************************************************/

#include "svga_cmd.h"

#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "os/os_thread.h"
#include "util/u_bitmask.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "svga_format.h"
#include "svga_screen.h"
#include "svga_context.h"
#include "svga_sampler_view.h"
#include "svga_resource_texture.h"
#include "svga_surface.h"
#include "svga_debug.h"

static void svga_mark_surface_dirty(struct pipe_surface *surf);

void
svga_texture_copy_handle(struct svga_context *svga,
                         struct svga_winsys_surface *src_handle,
                         unsigned src_x, unsigned src_y, unsigned src_z,
                         unsigned src_level, unsigned src_layer,
                         struct svga_winsys_surface *dst_handle,
                         unsigned dst_x, unsigned dst_y, unsigned dst_z,
                         unsigned dst_level, unsigned dst_layer,
                         unsigned width, unsigned height, unsigned depth)
{
   struct svga_surface dst, src;
   enum pipe_error ret;
   SVGA3dCopyBox box, *boxes;

   assert(svga);

   src.handle = src_handle;
   src.real_level = src_level;
   src.real_layer = src_layer;
   src.real_zslice = 0;

   dst.handle = dst_handle;
   dst.real_level = dst_level;
   dst.real_layer = dst_layer;
   dst.real_zslice = 0;

   box.x = dst_x;
   box.y = dst_y;
   box.z = dst_z;
   box.w = width;
   box.h = height;
   box.d = depth;
   box.srcx = src_x;
   box.srcy = src_y;
   box.srcz = src_z;

/*
   SVGA_DBG(DEBUG_VIEWS, "mipcopy src: %p %u (%ux%ux%u), dst: %p %u (%ux%ux%u)\n",
            src_handle, src_level, src_x, src_y, src_z,
            dst_handle, dst_level, dst_x, dst_y, dst_z);
*/

   ret = SVGA3D_BeginSurfaceCopy(svga->swc,
                                 &src.base,
                                 &dst.base,
                                 &boxes, 1);
   if (ret != PIPE_OK) {
      svga_context_flush(svga, NULL);
      ret = SVGA3D_BeginSurfaceCopy(svga->swc,
                                    &src.base,
                                    &dst.base,
                                    &boxes, 1);
      assert(ret == PIPE_OK);
   }
   *boxes = box;
   SVGA_FIFOCommitAll(svga->swc);
}


struct svga_winsys_surface *
svga_texture_view_surface(struct svga_context *svga,
                          struct svga_texture *tex,
                          unsigned bind_flags,
                          SVGA3dSurfaceFlags flags,
                          SVGA3dSurfaceFormat format,
                          unsigned start_mip,
                          unsigned num_mip,
                          int layer_pick,
                          unsigned num_layers,
                          int zslice_pick,
                          struct svga_host_surface_cache_key *key) /* OUT */
{
   struct svga_screen *ss = svga_screen(svga->pipe.screen);
   struct svga_winsys_surface *handle;
   uint32_t i, j;
   unsigned z_offset = 0;

   SVGA_DBG(DEBUG_PERF,
            "svga: Create surface view: layer %d zslice %d mips %d..%d\n",
            layer_pick, zslice_pick, start_mip, start_mip+num_mip-1);

   key->flags = flags;
   key->format = format;
   key->numMipLevels = num_mip;
   key->size.width = u_minify(tex->b.b.width0, start_mip);
   key->size.height = u_minify(tex->b.b.height0, start_mip);
   key->size.depth = zslice_pick < 0 ? u_minify(tex->b.b.depth0, start_mip) : 1;
   key->cachable = 1;
   key->arraySize = 1;
   key->numFaces = 1;

   /* single sample surface can be treated as non-multisamples surface */
   key->sampleCount = tex->b.b.nr_samples > 1 ? tex->b.b.nr_samples : 0;

   if (key->sampleCount > 1) {
      key->flags |= SVGA3D_SURFACE_MASKABLE_ANTIALIAS;
   }

   if (tex->b.b.target == PIPE_TEXTURE_CUBE && layer_pick < 0) {
      key->flags |= SVGA3D_SURFACE_CUBEMAP;
      key->numFaces = 6;
   } else if (tex->b.b.target == PIPE_TEXTURE_1D_ARRAY ||
              tex->b.b.target == PIPE_TEXTURE_2D_ARRAY) {
      key->arraySize = num_layers;
   }

   if (key->format == SVGA3D_FORMAT_INVALID) {
      key->cachable = 0;
      return NULL;
   }

   SVGA_DBG(DEBUG_DMA, "surface_create for texture view\n");
   handle = svga_screen_surface_create(ss, bind_flags, PIPE_USAGE_DEFAULT, key);
   if (!handle) {
      key->cachable = 0;
      return NULL;
   }

   SVGA_DBG(DEBUG_DMA, " --> got sid %p (texture view)\n", handle);

   if (layer_pick < 0)
      layer_pick = 0;

   if (zslice_pick >= 0)
      z_offset = zslice_pick;

   for (i = 0; i < key->numMipLevels; i++) {
      for (j = 0; j < key->numFaces * key->arraySize; j++) {
         if (svga_is_texture_level_defined(tex, j + layer_pick, i + start_mip)) {
            unsigned depth = (zslice_pick < 0 ?
                              u_minify(tex->b.b.depth0, i + start_mip) :
                              1);

            svga_texture_copy_handle(svga,
                                     tex->handle,
                                     0, 0, z_offset,
                                     i + start_mip,
                                     j + layer_pick,
                                     handle, 0, 0, 0, i, j,
                                     u_minify(tex->b.b.width0, i + start_mip),
                                     u_minify(tex->b.b.height0, i + start_mip),
                                     depth);
         }
      }
   }

   return handle;
}


/**
 * A helper function to create a surface view.
 * The view boolean flag specifies whether svga_texture_view_surface()
 * will be called to create a cloned surface and resource for the view.
 */
static struct pipe_surface *
svga_create_surface_view(struct pipe_context *pipe,
                         struct pipe_resource *pt,
                         const struct pipe_surface *surf_tmpl,
                         boolean view)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_texture *tex = svga_texture(pt);
   struct pipe_screen *screen = pipe->screen;
   struct svga_screen *ss = svga_screen(screen);
   struct svga_surface *s;
   unsigned layer, zslice, bind;
   unsigned nlayers = 1;
   SVGA3dSurfaceFlags flags = 0;
   SVGA3dSurfaceFormat format;
   struct pipe_surface *retVal = NULL;

   s = CALLOC_STRUCT(svga_surface);
   if (!s)
      return NULL;

   SVGA_STATS_TIME_PUSH(ss->sws, SVGA_STATS_TIME_CREATESURFACEVIEW);

   if (pt->target == PIPE_TEXTURE_CUBE) {
      layer = surf_tmpl->u.tex.first_layer;
      zslice = 0;
   }
   else if (pt->target == PIPE_TEXTURE_1D_ARRAY ||
            pt->target == PIPE_TEXTURE_2D_ARRAY) {
      layer = surf_tmpl->u.tex.first_layer;
      zslice = 0;
      nlayers = surf_tmpl->u.tex.last_layer - surf_tmpl->u.tex.first_layer + 1;
   }
   else {
      layer = 0;
      zslice = surf_tmpl->u.tex.first_layer;
   }

   pipe_reference_init(&s->base.reference, 1);
   pipe_resource_reference(&s->base.texture, pt);
   s->base.context = pipe;
   s->base.format = surf_tmpl->format;
   s->base.width = u_minify(pt->width0, surf_tmpl->u.tex.level);
   s->base.height = u_minify(pt->height0, surf_tmpl->u.tex.level);
   s->base.u.tex.level = surf_tmpl->u.tex.level;
   s->base.u.tex.first_layer = surf_tmpl->u.tex.first_layer;
   s->base.u.tex.last_layer = surf_tmpl->u.tex.last_layer;
   s->view_id = SVGA3D_INVALID_ID;

   s->backed = NULL;

   if (util_format_is_depth_or_stencil(surf_tmpl->format)) {
      flags = SVGA3D_SURFACE_HINT_DEPTHSTENCIL |
              SVGA3D_SURFACE_BIND_DEPTH_STENCIL;
      bind = PIPE_BIND_DEPTH_STENCIL;
   }
   else {
      flags = SVGA3D_SURFACE_HINT_RENDERTARGET |
              SVGA3D_SURFACE_BIND_RENDER_TARGET;
      bind = PIPE_BIND_RENDER_TARGET;
   }

   if (tex->imported)
      format = tex->key.format;
   else
      format = svga_translate_format(ss, surf_tmpl->format, bind);

   assert(format != SVGA3D_FORMAT_INVALID);

   if (view) {
      SVGA_DBG(DEBUG_VIEWS, "svga: Surface view: yes %p, level %u layer %u z %u, %p\n",
               pt, surf_tmpl->u.tex.level, layer, zslice, s);

      if (svga_have_vgpu10(svga)) {
         switch (pt->target) {
         case PIPE_TEXTURE_1D:
            flags |= SVGA3D_SURFACE_1D;
            break;
         case PIPE_TEXTURE_1D_ARRAY:
            flags |= SVGA3D_SURFACE_1D | SVGA3D_SURFACE_ARRAY;
            break;
         case PIPE_TEXTURE_2D_ARRAY:
            flags |= SVGA3D_SURFACE_ARRAY;
            break;
         case PIPE_TEXTURE_3D:
            flags |= SVGA3D_SURFACE_VOLUME;
            break;
         case PIPE_TEXTURE_CUBE:
            if (nlayers == 6)
               flags |= SVGA3D_SURFACE_CUBEMAP;
            break;
         default:
            break;
         }
      }

      /* When we clone the surface view resource, use the format used in
       * the creation of the original resource.
       */
      s->handle = svga_texture_view_surface(svga, tex, bind, flags,
                                            tex->key.format,
                                            surf_tmpl->u.tex.level, 1,
                                            layer, nlayers, zslice, &s->key);
      if (!s->handle) {
         FREE(s);
         goto done;
      }

      s->key.format = format;
      s->real_layer = 0;
      s->real_level = 0;
      s->real_zslice = 0;
   } else {
      SVGA_DBG(DEBUG_VIEWS,
               "svga: Surface view: no %p, level %u, layer %u, z %u, %p\n",
               pt, surf_tmpl->u.tex.level, layer, zslice, s);

      memset(&s->key, 0, sizeof s->key);
      s->key.format = format;
      s->handle = tex->handle;
      s->real_layer = layer;
      s->real_zslice = zslice;
      s->real_level = surf_tmpl->u.tex.level;
   }

   svga->hud.num_surface_views++;
   retVal = &s->base;

done:
   SVGA_STATS_TIME_POP(ss->sws);
   return retVal;
}


static struct pipe_surface *
svga_create_surface(struct pipe_context *pipe,
                    struct pipe_resource *pt,
                    const struct pipe_surface *surf_tmpl)
{
   struct svga_context *svga = svga_context(pipe);
   struct pipe_screen *screen = pipe->screen;
   struct pipe_surface *surf = NULL;
   boolean view = FALSE;

   SVGA_STATS_TIME_PUSH(svga_sws(svga), SVGA_STATS_TIME_CREATESURFACE);

   if (svga_screen(screen)->debug.force_surface_view)
      view = TRUE;

   if (surf_tmpl->u.tex.level != 0 &&
       svga_screen(screen)->debug.force_level_surface_view)
      view = TRUE;

   if (pt->target == PIPE_TEXTURE_3D)
      view = TRUE;

   if (svga_have_vgpu10(svga) || svga_screen(screen)->debug.no_surface_view)
      view = FALSE;

   surf = svga_create_surface_view(pipe, pt, surf_tmpl, view);

   SVGA_STATS_TIME_POP(svga_sws(svga));

   return surf;
}


/**
 * Clone the surface view and its associated resource.
 */
static struct svga_surface *
create_backed_surface_view(struct svga_context *svga, struct svga_surface *s)
{
   SVGA_STATS_TIME_PUSH(svga_sws(svga),
                        SVGA_STATS_TIME_CREATEBACKEDSURFACEVIEW);

   if (!s->backed) {
      struct svga_texture *tex = svga_texture(s->base.texture);
      struct pipe_surface *backed_view;

      backed_view = svga_create_surface_view(&svga->pipe,
                                             &tex->b.b,
                                             &s->base,
                                             TRUE);
      if (!backed_view)
         return NULL;

      s->backed = svga_surface(backed_view);
   }

   svga_mark_surface_dirty(&s->backed->base);

   SVGA_STATS_TIME_POP(svga_sws(svga));

   return s->backed;
}

/**
 * Create a DX RenderTarget/DepthStencil View for the given surface,
 * if needed.
 */
struct pipe_surface *
svga_validate_surface_view(struct svga_context *svga, struct svga_surface *s)
{
   enum pipe_error ret = PIPE_OK;
   enum pipe_shader_type shader;

   assert(svga_have_vgpu10(svga));
   assert(s);

   SVGA_STATS_TIME_PUSH(svga_sws(svga),
                        SVGA_STATS_TIME_VALIDATESURFACEVIEW);

   /**
    * DX spec explicitly specifies that no resource can be bound to a render
    * target view and a shader resource view simultanously.
    * So first check if the resource bound to this surface view collides with
    * a sampler view. If so, then we will clone this surface view and its
    * associated resource. We will then use the cloned surface view for
    * render target.
    */
   for (shader = PIPE_SHADER_VERTEX; shader <= PIPE_SHADER_GEOMETRY; shader++) {
      if (svga_check_sampler_view_resource_collision(svga, s->handle, shader)) {
         SVGA_DBG(DEBUG_VIEWS,
                  "same resource used in shaderResource and renderTarget 0x%x\n",
                  s->handle);
         s = create_backed_surface_view(svga, s);
         /* s may be null here if the function failed */
         break;
      }
   }

   if (s && s->view_id == SVGA3D_INVALID_ID) {
      SVGA3dResourceType resType;
      SVGA3dRenderTargetViewDesc desc;

      desc.tex.mipSlice = s->real_level;
      desc.tex.firstArraySlice = s->real_layer + s->real_zslice;
      desc.tex.arraySize =
         s->base.u.tex.last_layer - s->base.u.tex.first_layer + 1;

      s->view_id = util_bitmask_add(svga->surface_view_id_bm);

      resType = svga_resource_type(s->base.texture->target);

      if (util_format_is_depth_or_stencil(s->base.format)) {
         ret = SVGA3D_vgpu10_DefineDepthStencilView(svga->swc,
                                                    s->view_id,
                                                    s->handle,
                                                    s->key.format,
                                                    resType,
                                                    &desc);
      }
      else {
         SVGA3dSurfaceFormat view_format = s->key.format;
         const struct svga_texture *stex = svga_texture(s->base.texture);

         /* Can't create RGBA render target view of a RGBX surface so adjust
          * the view format.  We do something similar for texture samplers in
          * svga_validate_pipe_sampler_view().
          */
         if (view_format == SVGA3D_B8G8R8A8_UNORM &&
             stex->key.format == SVGA3D_B8G8R8X8_TYPELESS) {
            view_format = SVGA3D_B8G8R8X8_UNORM;
         }

         ret = SVGA3D_vgpu10_DefineRenderTargetView(svga->swc,
                                                    s->view_id,
                                                    s->handle,
                                                    view_format,
                                                    resType,
                                                    &desc);
      }

      if (ret != PIPE_OK) {
         util_bitmask_clear(svga->surface_view_id_bm, s->view_id);
         s->view_id = SVGA3D_INVALID_ID;
         s = NULL;
      }
   }
   
   SVGA_STATS_TIME_POP(svga_sws(svga));

   return s ? &s->base : NULL;
}



static void
svga_surface_destroy(struct pipe_context *pipe,
                     struct pipe_surface *surf)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_surface *s = svga_surface(surf);
   struct svga_texture *t = svga_texture(surf->texture);
   struct svga_screen *ss = svga_screen(surf->texture->screen);
   enum pipe_error ret = PIPE_OK;

   SVGA_STATS_TIME_PUSH(ss->sws, SVGA_STATS_TIME_DESTROYSURFACE);

   /* Destroy the backed view surface if it exists */
   if (s->backed) {
      svga_surface_destroy(pipe, &s->backed->base);
      s->backed = NULL;
   }

   if (s->handle != t->handle) {
      SVGA_DBG(DEBUG_DMA, "unref sid %p (tex surface)\n", s->handle);
      svga_screen_surface_destroy(ss, &s->key, &s->handle);
   }

   if (s->view_id != SVGA3D_INVALID_ID) {
      unsigned try;

      assert(svga_have_vgpu10(svga));
      for (try = 0; try < 2; try++) {
         if (util_format_is_depth_or_stencil(s->base.format)) {
            ret = SVGA3D_vgpu10_DestroyDepthStencilView(svga->swc, s->view_id);
         }
         else {
            ret = SVGA3D_vgpu10_DestroyRenderTargetView(svga->swc, s->view_id);
         }
         if (ret == PIPE_OK)
            break;
         svga_context_flush(svga, NULL);
      }
      assert(ret == PIPE_OK);
      util_bitmask_clear(svga->surface_view_id_bm, s->view_id);
   }

   pipe_resource_reference(&surf->texture, NULL);
   FREE(surf);

   svga->hud.num_surface_views--;
   SVGA_STATS_TIME_POP(ss->sws);
}


static void
svga_mark_surface_dirty(struct pipe_surface *surf)
{
   struct svga_surface *s = svga_surface(surf);
   struct svga_texture *tex = svga_texture(surf->texture);

   if (!s->dirty) {
      s->dirty = TRUE;

      if (s->handle == tex->handle) {
         /* hmm so 3d textures always have all their slices marked ? */
         svga_define_texture_level(tex, surf->u.tex.first_layer,
                                   surf->u.tex.level);
      }
      else {
         /* this will happen later in svga_propagate_surface */
      }
   }

   /* Increment the view_age and texture age for this surface's mipmap
    * level so that any sampler views into the texture are re-validated too.
    */
   svga_age_texture_view(tex, surf->u.tex.level);
}


void
svga_mark_surfaces_dirty(struct svga_context *svga)
{
   unsigned i;

   for (i = 0; i < svga->curr.framebuffer.nr_cbufs; i++) {
      if (svga->curr.framebuffer.cbufs[i])
         svga_mark_surface_dirty(svga->curr.framebuffer.cbufs[i]);
   }
   if (svga->curr.framebuffer.zsbuf)
      svga_mark_surface_dirty(svga->curr.framebuffer.zsbuf);
}


/**
 * Progagate any changes from surfaces to texture.
 * pipe is optional context to inline the blit command in.
 */
void
svga_propagate_surface(struct svga_context *svga, struct pipe_surface *surf)
{
   struct svga_surface *s = svga_surface(surf);
   struct svga_texture *tex = svga_texture(surf->texture);
   struct svga_screen *ss = svga_screen(surf->texture->screen);

   if (!s->dirty)
      return;

   SVGA_STATS_TIME_PUSH(ss->sws, SVGA_STATS_TIME_PROPAGATESURFACE);

   s->dirty = FALSE;
   ss->texture_timestamp++;
   svga_age_texture_view(tex, surf->u.tex.level);

   if (s->handle != tex->handle) {
      unsigned zslice, layer;
      unsigned nlayers = 1;
      unsigned i;

      if (surf->texture->target == PIPE_TEXTURE_CUBE) {
         zslice = 0;
         layer = surf->u.tex.first_layer;
      }
      else if (surf->texture->target == PIPE_TEXTURE_1D_ARRAY ||
               surf->texture->target == PIPE_TEXTURE_2D_ARRAY) {
         zslice = 0;
         layer = surf->u.tex.first_layer;
         nlayers = surf->u.tex.last_layer - surf->u.tex.first_layer + 1;
      }
      else {
         zslice = surf->u.tex.first_layer;
         layer = 0;
      }

      SVGA_DBG(DEBUG_VIEWS,
               "svga: Surface propagate: tex %p, level %u, from %p\n",
               tex, surf->u.tex.level, surf);
      for (i = 0; i < nlayers; i++) {
         svga_texture_copy_handle(svga,
                                  s->handle, 0, 0, 0, s->real_level,
                                  s->real_layer + i,
                                  tex->handle, 0, 0, zslice, surf->u.tex.level,
                                  layer + i,
                                  u_minify(tex->b.b.width0, surf->u.tex.level),
                                  u_minify(tex->b.b.height0, surf->u.tex.level),
                                  1);
         svga_define_texture_level(tex, layer + i, surf->u.tex.level);
      }
   }

   SVGA_STATS_TIME_POP(ss->sws);
}


/**
 * If any of the render targets are in backing texture views, propagate any
 * changes to them back to the original texture.
 */
void
svga_propagate_rendertargets(struct svga_context *svga)
{
   unsigned i;

   /* Note that we examine the svga->state.hw_draw.framebuffer surfaces,
    * not the svga->curr.framebuffer surfaces, because it's the former
    * surfaces which may be backing surface views (the actual render targets).
    */
   for (i = 0; i < svga->state.hw_draw.num_rendertargets; i++) {
      struct pipe_surface *s = svga->state.hw_draw.rtv[i];
      if (s) {
         svga_propagate_surface(svga, s);
      }
   }

   if (svga->state.hw_draw.dsv) {
      svga_propagate_surface(svga, svga->state.hw_draw.dsv);
   }
}


/**
 * Check if we should call svga_propagate_surface on the surface.
 */
boolean
svga_surface_needs_propagation(const struct pipe_surface *surf)
{
   const struct svga_surface *s = svga_surface_const(surf);
   struct svga_texture *tex = svga_texture(surf->texture);

   return s->dirty && s->handle != tex->handle;
}


static void
svga_get_sample_position(struct pipe_context *context,
                         unsigned sample_count, unsigned sample_index,
                         float *pos_out)
{
   /* We can't actually query the device to learn the sample positions.
    * These were grabbed from nvidia's driver.
    */
   static const float pos1[1][2] = {
      { 0.5, 0.5 }
   };
   static const float pos4[4][2] = {
      { 0.375000, 0.125000 },
      { 0.875000, 0.375000 },
      { 0.125000, 0.625000 },
      { 0.625000, 0.875000 }
   };
   static const float pos8[8][2] = {
      { 0.562500, 0.312500 },
      { 0.437500, 0.687500 },
      { 0.812500, 0.562500 },
      { 0.312500, 0.187500 },
      { 0.187500, 0.812500 },
      { 0.062500, 0.437500 },
      { 0.687500, 0.937500 },
      { 0.937500, 0.062500 }
   };
   static const float pos16[16][2] = {
      { 0.187500, 0.062500 },
      { 0.437500, 0.187500 },
      { 0.062500, 0.312500 },
      { 0.312500, 0.437500 },
      { 0.687500, 0.062500 },
      { 0.937500, 0.187500 },
      { 0.562500, 0.312500 },
      { 0.812500, 0.437500 },
      { 0.187500, 0.562500 },
      { 0.437500, 0.687500 },
      { 0.062500, 0.812500 },
      { 0.312500, 0.937500 },
      { 0.687500, 0.562500 },
      { 0.937500, 0.687500 },
      { 0.562500, 0.812500 },
      { 0.812500, 0.937500 }
   };
   const float (*positions)[2];

   switch (sample_count) {
   case 4:
      positions = pos4;
      break;
   case 8:
      positions = pos8;
      break;
   case 16:
      positions = pos16;
      break;
   default:
      positions = pos1;
   }

   pos_out[0] = positions[sample_index][0];
   pos_out[1] = positions[sample_index][1];
}


void
svga_init_surface_functions(struct svga_context *svga)
{
   svga->pipe.create_surface = svga_create_surface;
   svga->pipe.surface_destroy = svga_surface_destroy;
   svga->pipe.get_sample_position = svga_get_sample_position;
}
