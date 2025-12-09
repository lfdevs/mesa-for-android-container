/*
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2018-2019 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/compiler.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/u_math.h"
#include "util/u_debug.h"
#include "util/u_transfer.h"

#include "lima_bo.h"
#include "lima_context.h"
#include "lima_pack.h"
#include "lima_screen.h"
#include "lima_texture.h"
#include "lima_resource.h"
#include "lima_job.h"
#include "lima_util.h"
#include "lima_format.h"

#include <drm-uapi/lima_drm.h>


#define lima_tex_list_size 64

/*
 * Note: this function is used by both draw and flush code path,
 * make sure no lima_job_get() is called inside this.
 */
void
lima_texture_desc_set_res(struct lima_context *ctx, struct LIMA_TEXTURE_DESCRIPTOR *desc,
                          struct pipe_resource *prsc,
                          unsigned first_level, unsigned last_level,
                          unsigned first_layer, unsigned mrt_idx)
{
   struct lima_resource *lima_res = lima_resource(prsc);

   desc->width = prsc->width0;
   desc->height = prsc->height0;
   desc->depth = prsc->depth0;
   if (first_level != 0) {
      desc->width = u_minify(desc->width, first_level);
      desc->height = u_minify(desc->height, first_level);
      desc->depth = u_minify(desc->depth, first_level);
   }

   desc->texel_format = lima_format_get_texel(prsc->format);
   desc->swap_rb = lima_format_get_texel_swap_rb(prsc->format);

   if (lima_res->tiled)
      desc->layout = LIMA_TEXTURE_LAYOUT_TILED;
   else {
      desc->stride = lima_res->levels[first_level].stride;
      desc->has_stride = true;
      desc->layout = LIMA_TEXTURE_LAYOUT_LINEAR;
   }

   uint32_t base_va = lima_res->bo->va;

   uint32_t *mips[] = {&desc->mip_0, &desc->mip_1, 
                       &desc->mip_2, &desc->mip_3,
                       &desc->mip_4, &desc->mip_5,
                       &desc->mip_6, &desc->mip_7,
                       &desc->mip_8, &desc->mip_9,
                       &desc->mip_10};


   int max_mips = MIN2(last_level - first_level, ARRAY_SIZE(mips) - 1);

   for (int i = 0; i <= max_mips; i++) {
      *mips[i] = base_va + lima_res->levels[first_level + i].offset;
   }

   *mips[0] += first_layer * lima_res->levels[first_level].layer_stride +
               mrt_idx * lima_res->mrt_pitch;
}

static unsigned
pipe_wrap_to_lima(unsigned pipe_wrap, bool using_nearest)
{
   switch (pipe_wrap) {
   case PIPE_TEX_WRAP_REPEAT:
      return LIMA_TEX_WRAP_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
      return LIMA_TEX_WRAP_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP:
      if (using_nearest)
         return LIMA_TEX_WRAP_CLAMP_TO_EDGE;
      else
         return LIMA_TEX_WRAP_CLAMP;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      return LIMA_TEX_WRAP_CLAMP_TO_BORDER;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      return LIMA_TEX_WRAP_MIRROR_REPEAT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
      return LIMA_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
      if (using_nearest)
         return LIMA_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
      else
         return LIMA_TEX_WRAP_MIRROR_CLAMP;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
      return LIMA_TEX_WRAP_MIRROR_CLAMP_TO_BORDER;
   default:
      return LIMA_TEX_WRAP_REPEAT;
   }
}

static void
lima_update_tex_desc(struct lima_context *ctx, struct lima_sampler_state *sampler,
                     struct lima_sampler_view *texture, void *pdesc,
                     unsigned desc_size)
{
   if (!texture) {
      memset(pdesc, 0, desc_size);
      return;
   }

   lima_pack(pdesc, TEXTURE_DESCRIPTOR, desc)
   {
      float lod_bias_delta = 0.0;
      unsigned first_level;
      unsigned last_level;
      unsigned first_layer;
      float max_lod;

      switch (texture->base.target) {
      case PIPE_TEXTURE_1D:
         desc.sampler_dim = LIMA_SAMPLER_DIMENSION_1D;
         break;
      case PIPE_TEXTURE_2D:
      case PIPE_TEXTURE_RECT:
         desc.sampler_dim = LIMA_SAMPLER_DIMENSION_2D;
         break;
      case PIPE_TEXTURE_CUBE:
         desc.cube_map = true;
         FALLTHROUGH;
      case PIPE_TEXTURE_3D:
         desc.sampler_dim = LIMA_SAMPLER_DIMENSION_3D;
         break;
      default:
         break;
      }

      if (sampler->base.unnormalized_coords)
         desc.unnorm_coords = true;

      first_level = texture->base.u.tex.first_level;
      last_level = texture->base.u.tex.last_level;
      first_layer = texture->base.u.tex.first_layer;
      if (last_level - first_level >= LIMA_MAX_MIP_LEVELS)
         last_level = first_level + LIMA_MAX_MIP_LEVELS - 1;

      desc.min_lod = sampler->base.min_lod;
      max_lod = MIN2(sampler->base.max_lod, sampler->base.min_lod +
                                            (last_level - first_level));
      desc.max_lod = max_lod;
      desc.lod_bias = sampler->base.lod_bias;

      desc.max_anisotropy = sampler->base.max_anisotropy;

      switch (sampler->base.min_mip_filter) {
         case PIPE_TEX_MIPFILTER_LINEAR:
            desc.mipfilter = LIMA_MIPFILTER_LINEAR;
            break;
         case PIPE_TEX_MIPFILTER_NEAREST:
            desc.mipfilter = LIMA_MIPFILTER_NEAREST;
            break;
         case PIPE_TEX_MIPFILTER_NONE:
            desc.max_lod = desc.min_lod;
            break;
         default:
            break;
      }

      switch (sampler->base.mag_img_filter) {
      case PIPE_TEX_FILTER_LINEAR:
         desc.mag_img_filter_nearest = false;
         break;
      case PIPE_TEX_FILTER_NEAREST:
      default:
         desc.mag_img_filter_nearest = true;
         break;
      }

      switch (sampler->base.min_img_filter) {
         break;
      case PIPE_TEX_FILTER_LINEAR:
         desc.min_img_filter_nearest = false;
         break;
      case PIPE_TEX_FILTER_NEAREST:
      default:
         lod_bias_delta = 0.5;
         desc.min_img_filter_nearest = true;
         break;
      }

      /* Panfrost mentions that GL_CLAMP is broken for NEAREST filter on Midgard,
       * looks like it also broken on Utgard, since it fails in piglit
       */
      bool using_nearest = sampler->base.min_img_filter == PIPE_TEX_FILTER_NEAREST;

      desc.wrap_s = pipe_wrap_to_lima(sampler->base.wrap_s, using_nearest);
      desc.wrap_t = pipe_wrap_to_lima(sampler->base.wrap_t, using_nearest);
      desc.wrap_r = pipe_wrap_to_lima(sampler->base.wrap_r, using_nearest);

      desc.border_red = sampler->base.border_color.f[0];
      desc.border_green = sampler->base.border_color.f[1];
      desc.border_blue = sampler->base.border_color.f[2];
      desc.border_alpha = sampler->base.border_color.f[3];

      if (desc.min_img_filter_nearest && desc.mag_img_filter_nearest &&
          desc.mipfilter == 0 &&
          (desc.min_lod != desc.max_lod))
         lod_bias_delta = -1.0/16.0;

      desc.lod_bias += lod_bias_delta;

      lima_texture_desc_set_res(ctx, &desc, texture->base.texture,
                                first_level, last_level, first_layer, 0);
   }
}

static unsigned
lima_calc_tex_desc_size(struct lima_sampler_view *texture)
{
   return sizeof(struct lima_texture_descriptor_packed);
}

void
lima_update_textures(struct lima_context *ctx)
{
   struct lima_job *job = lima_job_get(ctx);
   struct lima_texture_stateobj *lima_tex = &ctx->tex_stateobj;

   assert (lima_tex->num_samplers <= 16);

   /* Nothing to do - we have no samplers or textures */
   if (!lima_tex->num_samplers || !lima_tex->num_textures)
      return;

   /* we always need to add texture bo to job */
   for (int i = 0; i < lima_tex->num_samplers; i++) {
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);
      if (!texture)
         continue;
      struct lima_resource *rsc = lima_resource(texture->base.texture);
      lima_flush_previous_job_writing_resource(ctx, texture->base.texture);
      lima_job_add_bo(job, LIMA_PIPE_PP, rsc->bo, LIMA_SUBMIT_BO_READ);
   }

   /* do not regenerate texture desc if no change */
   if (!(ctx->dirty & LIMA_CONTEXT_DIRTY_TEXTURES))
      return;

   unsigned size = lima_tex_list_size;
   for (int i = 0; i < lima_tex->num_samplers; i++) {
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);
      size += lima_calc_tex_desc_size(texture);
   }

   uint32_t *descs =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_pp_tex_desc, size);

   off_t offset = lima_tex_list_size;
   for (int i = 0; i < lima_tex->num_samplers; i++) {
      struct lima_sampler_state *sampler = lima_sampler_state(lima_tex->samplers[i]);
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);
      unsigned desc_size = lima_calc_tex_desc_size(texture);

      descs[i] = lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc) + offset;
      lima_update_tex_desc(ctx, sampler, texture, (void *)descs + offset, desc_size);
      offset += desc_size;
   }

   lima_dump_command_stream_print(
      job->dump, descs, size, false, "add textures_desc at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc));

   lima_dump_texture_descriptor(
      job->dump, descs, size,
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc) + lima_tex_list_size,
      lima_tex_list_size);
}
