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

#include "svga_context.h"
#include "svga_debug.h"
#include "svga_cmd.h"
#include "svga_format.h"
#include "svga_resource_buffer.h"
#include "svga_resource_texture.h"
#include "svga_surface.h"

//#include "util/u_blit_sw.h"
#include "util/u_format.h"
#include "util/u_surface.h"

#define FILE_DEBUG_FLAG DEBUG_BLIT


/**
 * Copy an image between textures with the vgpu10 CopyRegion command.
 */
static void
copy_region_vgpu10(struct svga_context *svga, struct pipe_resource *src_tex,
                    unsigned src_x, unsigned src_y, unsigned src_z,
                    unsigned src_level, unsigned src_face,
                    struct pipe_resource *dst_tex,
                    unsigned dst_x, unsigned dst_y, unsigned dst_z,
                    unsigned dst_level, unsigned dst_face,
                    unsigned width, unsigned height, unsigned depth)
{
   enum pipe_error ret;
   uint32 srcSubResource, dstSubResource;
   struct svga_texture *dtex, *stex;
   SVGA3dCopyBox box;

   stex = svga_texture(src_tex);
   dtex = svga_texture(dst_tex);

   box.x = dst_x;
   box.y = dst_y;
   box.z = dst_z;
   box.w = width;
   box.h = height;
   box.d = depth;
   box.srcx = src_x;
   box.srcy = src_y;
   box.srcz = src_z;

   srcSubResource = src_face * (src_tex->last_level + 1) + src_level;
   dstSubResource = dst_face * (dst_tex->last_level + 1) + dst_level;

   ret = SVGA3D_vgpu10_PredCopyRegion(svga->swc,
                                      dtex->handle, dstSubResource,
                                      stex->handle, srcSubResource, &box);
   if (ret != PIPE_OK) {
      svga_context_flush(svga, NULL);
      ret = SVGA3D_vgpu10_PredCopyRegion(svga->swc,
                                         dtex->handle, dstSubResource,
                                         stex->handle, srcSubResource, &box);
      assert(ret == PIPE_OK);
   }

   svga_define_texture_level(dtex, dst_face, dst_level);
}


/**
 * For some texture types, we need to move the z (slice) coordinate
 * to the layer value.  For example, to select the z=3 slice of a 2D ARRAY
 * texture, we need to use layer=3 and set z=0.
 */
static void
adjust_z_layer(enum pipe_texture_target target,
               int z_in, unsigned *layer_out, unsigned *z_out)
{
   if (target == PIPE_TEXTURE_CUBE ||
       target == PIPE_TEXTURE_2D_ARRAY ||
       target == PIPE_TEXTURE_1D_ARRAY) {
      *layer_out = z_in;
      *z_out = 0;
   }
   else {
      *layer_out = 0;
      *z_out = z_in;
   }
}


static void
svga_resource_copy_region(struct pipe_context *pipe,
                          struct pipe_resource *dst_tex,
                          unsigned dst_level,
                          unsigned dstx, unsigned dsty, unsigned dstz,
                          struct pipe_resource *src_tex,
                          unsigned src_level,
                          const struct pipe_box *src_box)
{
   struct svga_context *svga = svga_context(pipe);
   struct svga_texture *stex, *dtex;
   unsigned dst_face_layer, dst_z, src_face_layer, src_z;

   /* Emit buffered drawing commands, and any back copies.
    */
   svga_surfaces_flush( svga );

   if (dst_tex->target == PIPE_BUFFER && src_tex->target == PIPE_BUFFER) {
      /* can't copy within the same buffer, unfortunately */
      if (svga_have_vgpu10(svga) && src_tex != dst_tex) {
         enum pipe_error ret;
         struct svga_winsys_surface *src_surf;
         struct svga_winsys_surface *dst_surf;
         struct svga_buffer *dbuffer = svga_buffer(dst_tex);

         src_surf = svga_buffer_handle(svga, src_tex);
         dst_surf = svga_buffer_handle(svga, dst_tex);

         ret = SVGA3D_vgpu10_BufferCopy(svga->swc, src_surf, dst_surf,
                                        src_box->x, dstx, src_box->width);
         if (ret != PIPE_OK) {
            svga_context_flush(svga, NULL);
            ret = SVGA3D_vgpu10_BufferCopy(svga->swc, src_surf, dst_surf,
                                           src_box->x, dstx, src_box->width);
            assert(ret == PIPE_OK);
         }

         dbuffer->dirty = TRUE;
      }
      else {
         /* use map/memcpy fallback */
         util_resource_copy_region(pipe, dst_tex, dst_level, dstx,
                                   dsty, dstz, src_tex, src_level, src_box);
      }
      return;
   }

   stex = svga_texture(src_tex);
   dtex = svga_texture(dst_tex);

   adjust_z_layer(src_tex->target, src_box->z, &src_face_layer, &src_z);
   adjust_z_layer(dst_tex->target, dstz, &dst_face_layer, &dst_z);

   if (svga_have_vgpu10(svga)) {
      /* vgpu10 */
      if (util_format_is_compressed(src_tex->format) ==
          util_format_is_compressed(dst_tex->format) &&
          !util_format_is_depth_and_stencil(src_tex->format) &&
          stex->handle != dtex->handle &&
          svga_resource_type(src_tex->target) ==
          svga_resource_type(dst_tex->target)) {
         copy_region_vgpu10(svga,
                            src_tex,
                            src_box->x, src_box->y, src_z,
                            src_level, src_face_layer,
                            dst_tex,
                            dstx, dsty, dst_z,
                            dst_level, dst_face_layer,
                            src_box->width, src_box->height, src_box->depth);
      }
      else {
         util_resource_copy_region(pipe, dst_tex, dst_level, dstx, dsty, dstz,
                                   src_tex, src_level, src_box);
      }
   }
   else {
      /* vgpu9 */
      if (src_tex->format == dst_tex->format) {
         svga_texture_copy_handle(svga,
                                  stex->handle,
                                  src_box->x, src_box->y, src_z,
                                  src_level, src_face_layer,
                                  dtex->handle,
                                  dstx, dsty, dst_z,
                                   dst_level, dst_face_layer,
                                  src_box->width, src_box->height,
                                  src_box->depth);
      }
      else {
         util_resource_copy_region(pipe, dst_tex, dst_level, dstx, dsty, dstz,
                                   src_tex, src_level, src_box);
      }
   }

   /* Mark the destination image as being defined */
   svga_define_texture_level(dtex, dst_face_layer, dst_level);
}


/**
 * Are the given pipe formats compatible, in terms of vgpu10's
 * PredCopyRegion() command?
 */
static bool
formats_compatible(const struct svga_screen *ss,
                   enum pipe_format src_fmt,
                   enum pipe_format dst_fmt)
{
   SVGA3dSurfaceFormat src_svga_fmt, dst_svga_fmt;

   src_svga_fmt = svga_translate_format(ss, src_fmt, PIPE_BIND_SAMPLER_VIEW);
   dst_svga_fmt = svga_translate_format(ss, dst_fmt, PIPE_BIND_SAMPLER_VIEW);

   src_svga_fmt = svga_typeless_format(src_svga_fmt);
   dst_svga_fmt = svga_typeless_format(dst_svga_fmt);

   return src_svga_fmt == dst_svga_fmt;
}


/**
 * The state tracker implements some resource copies with blits (for
 * GL_ARB_copy_image).  This function checks if we should really do the blit
 * with a VGPU10 CopyRegion command or software fallback (for incompatible
 * src/dst formats).
 */
static bool
can_blit_via_copy_region_vgpu10(struct svga_context *svga,
                                const struct pipe_blit_info *blit_info)
{
   struct svga_texture *dtex, *stex;

   if (!svga_have_vgpu10(svga))
      return false;

   stex = svga_texture(blit_info->src.resource);
   dtex = svga_texture(blit_info->dst.resource);

   // can't copy within one resource
   if (stex->handle == dtex->handle)
      return false;

   /* can't copy between different resource types */
   if (svga_resource_type(blit_info->src.resource->target) !=
       svga_resource_type(blit_info->dst.resource->target))
      return false;

   /* check that the blit src/dst regions are same size, no flipping, etc. */
   if (blit_info->src.box.width != blit_info->dst.box.width ||
       blit_info->src.box.height != blit_info->dst.box.height)
      return false;

   /* depth/stencil copies not supported at this time */
   if (blit_info->mask != PIPE_MASK_RGBA)
      return false;

   if (blit_info->alpha_blend || blit_info->render_condition_enable ||
       blit_info->scissor_enable)
      return false;

   return formats_compatible(svga_screen(svga->pipe.screen),
                             blit_info->src.resource->format,
                             blit_info->dst.resource->format);
}


static void
svga_blit(struct pipe_context *pipe,
          const struct pipe_blit_info *blit_info)
{
   struct svga_context *svga = svga_context(pipe);
   struct pipe_blit_info blit = *blit_info;

   if (!svga_have_vgpu10(svga) &&
       blit.src.resource->nr_samples > 1 &&
       blit.dst.resource->nr_samples <= 1 &&
       !util_format_is_depth_or_stencil(blit.src.resource->format) &&
       !util_format_is_pure_integer(blit.src.resource->format)) {
      debug_printf("svga: color resolve unimplemented\n");
      return;
   }

   if (can_blit_via_copy_region_vgpu10(svga, blit_info)) {
      unsigned src_face, src_z, dst_face, dst_z;

      adjust_z_layer(blit.src.resource->target, blit.src.box.z,
                     &src_face, &src_z);

      adjust_z_layer(blit.dst.resource->target, blit.dst.box.z,
                     &dst_face, &dst_z);

      copy_region_vgpu10(svga,
                         blit.src.resource,
                         blit.src.box.x, blit.src.box.y, src_z,
                         blit.src.level, src_face,
                         blit.dst.resource,
                         blit.dst.box.x, blit.dst.box.y, dst_z,
                         blit.dst.level, dst_face,
                         blit.src.box.width, blit.src.box.height,
                         blit.src.box.depth);
      return;
   }

   if (util_can_blit_via_copy_region(blit_info, TRUE) ||
       util_can_blit_via_copy_region(blit_info, FALSE)) {
      util_resource_copy_region(pipe, blit.dst.resource,
                                blit.dst.level,
                                blit.dst.box.x, blit.dst.box.y,
                                blit.dst.box.z, blit.src.resource,
                                blit.src.level, &blit.src.box);
      return; /* done */
   }

   if ((blit.mask & PIPE_MASK_S) ||
       !util_blitter_is_blit_supported(svga->blitter, blit_info)) {
      debug_printf("svga: blit unsupported %s -> %s\n",
                   util_format_short_name(blit.src.resource->format),
                   util_format_short_name(blit.dst.resource->format));
      return;
   }

   /* XXX turn off occlusion and streamout queries */

   util_blitter_save_vertex_buffer_slot(svga->blitter, svga->curr.vb);
   util_blitter_save_vertex_elements(svga->blitter, (void*)svga->curr.velems);
   util_blitter_save_vertex_shader(svga->blitter, svga->curr.vs);
   util_blitter_save_geometry_shader(svga->blitter, svga->curr.user_gs);
   util_blitter_save_so_targets(svga->blitter, svga->num_so_targets,
                     (struct pipe_stream_output_target**)svga->so_targets);
   util_blitter_save_rasterizer(svga->blitter, (void*)svga->curr.rast);
   util_blitter_save_viewport(svga->blitter, &svga->curr.viewport);
   util_blitter_save_scissor(svga->blitter, &svga->curr.scissor);
   util_blitter_save_fragment_shader(svga->blitter, svga->curr.fs);
   util_blitter_save_blend(svga->blitter, (void*)svga->curr.blend);
   util_blitter_save_depth_stencil_alpha(svga->blitter,
                                         (void*)svga->curr.depth);
   util_blitter_save_stencil_ref(svga->blitter, &svga->curr.stencil_ref);
   util_blitter_save_sample_mask(svga->blitter, svga->curr.sample_mask);
   util_blitter_save_framebuffer(svga->blitter, &svga->curr.framebuffer);
   util_blitter_save_fragment_sampler_states(svga->blitter,
                     svga->curr.num_samplers[PIPE_SHADER_FRAGMENT],
                     (void**)svga->curr.sampler[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_fragment_sampler_views(svga->blitter,
                     svga->curr.num_sampler_views[PIPE_SHADER_FRAGMENT],
                     svga->curr.sampler_views[PIPE_SHADER_FRAGMENT]);
   /*util_blitter_save_render_condition(svga->blitter, svga->render_cond_query,
                                      svga->render_cond_cond, svga->render_cond_mode);*/
   util_blitter_blit(svga->blitter, &blit);
}


static void
svga_flush_resource(struct pipe_context *pipe,
                    struct pipe_resource *resource)
{
}


void
svga_init_blit_functions(struct svga_context *svga)
{
   svga->pipe.resource_copy_region = svga_resource_copy_region;
   svga->pipe.blit = svga_blit;
   svga->pipe.flush_resource = svga_flush_resource;
}
