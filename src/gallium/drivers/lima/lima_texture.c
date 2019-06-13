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

#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/u_math.h"
#include "util/u_debug.h"
#include "util/u_transfer.h"

#include "lima_bo.h"
#include "lima_context.h"
#include "lima_screen.h"
#include "lima_texture.h"
#include "lima_resource.h"
#include "lima_submit.h"
#include "lima_util.h"

#include <drm-uapi/lima_drm.h>

#define LIMA_TEXEL_FORMAT_BGR_565      0x0e
#define LIMA_TEXEL_FORMAT_Z16          0x12
#define LIMA_TEXEL_FORMAT_RGB_888      0x15
#define LIMA_TEXEL_FORMAT_RGBA_8888    0x16
#define LIMA_TEXEL_FORMAT_RGBX_8888    0x17
#define LIMA_TEXEL_FORMAT_Z24S8        0x2c

#define lima_tex_list_size 64

static uint32_t pipe_format_to_lima(enum pipe_format pformat)
{
   unsigned swap_chans = 0, flag1 = 0, format;

   switch (pformat) {
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      swap_chans = 1;
   case PIPE_FORMAT_B8G8R8A8_UNORM:
      format = LIMA_TEXEL_FORMAT_RGBA_8888;
      break;
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      swap_chans = 1;
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      format = LIMA_TEXEL_FORMAT_RGBX_8888;
      break;
   case PIPE_FORMAT_R8G8B8_UNORM:
      swap_chans = 1;
      format = LIMA_TEXEL_FORMAT_RGB_888;
      break;
   case PIPE_FORMAT_B5G6R5_UNORM:
      format = LIMA_TEXEL_FORMAT_BGR_565;
      break;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z24X8_UNORM:
      format = LIMA_TEXEL_FORMAT_Z24S8;
      break;
   case PIPE_FORMAT_Z16_UNORM:
      format = LIMA_TEXEL_FORMAT_Z16;
      break;
   default:
      assert(0);
      break;
   }

   return (swap_chans << 7) | (flag1 << 6) | format;
}

void
lima_texture_desc_set_res(struct lima_context *ctx, uint32_t *desc,
                          struct pipe_resource *prsc,
                          unsigned first_level, unsigned last_level)
{
   unsigned width, height, layout, i;
   struct lima_resource *lima_res = lima_resource(prsc);

   width = prsc->width0;
   height = prsc->height0;
   if (first_level != 0) {
      width = u_minify(width, first_level);
      height = u_minify(height, first_level);
   }

   desc[0] |= pipe_format_to_lima(prsc->format);
   desc[2] |= (width << 22);
   desc[3] |= 0x10000 | (height << 3) | (width >> 10);

   if (lima_res->tiled)
      layout = 3;
   else {
      /* for padded linear texture */
      if (lima_res->levels[first_level].width != width) {
         desc[0] |= lima_res->levels[first_level].width << 18;
         desc[2] |= 0x100;
      }
      layout = 0;
   }

   lima_submit_add_bo(ctx->pp_submit, lima_res->bo, LIMA_SUBMIT_BO_READ);

   uint32_t base_va = lima_res->bo->va;

   /* attach first level */
   uint32_t first_va = base_va + lima_res->levels[first_level].offset;
   desc[6] |= (first_va << 24) | (layout << 13);
   desc[7] |= first_va >> 8;

   /* Attach remaining levels.
    * Each subsequent mipmap address is specified using the 26 msbs.
    * These addresses are then packed continuously in memory */
   unsigned current_desc_index = 7;
   unsigned current_desc_bit_index = 24;
   for (i = first_level + 1; i <= last_level; i++) {
      uint32_t address = base_va + lima_res->levels[i].offset;
      address = (address >> 6);
      desc[current_desc_index] |= (address << current_desc_bit_index);
      if (current_desc_bit_index <= 6) {
         current_desc_bit_index += 26;
         if (current_desc_bit_index >= 32) {
            current_desc_bit_index &= 0x1F;
            current_desc_index++;
         }
         continue;
      }
      desc[current_desc_index + 1] |= (address >> (32 - current_desc_bit_index));
      current_desc_bit_index = (current_desc_bit_index + 26) & 0x1F;
      current_desc_index++;
   }
}

static void
lima_update_tex_desc(struct lima_context *ctx, struct lima_sampler_state *sampler,
                     struct lima_sampler_view *texture, void *pdesc)
{
   uint32_t *desc = pdesc;
   unsigned first_level;
   unsigned last_level;
   bool mipmapping;

   memset(desc, 0, lima_tex_desc_size);

   /* 2D texture */
   desc[1] |= 0x400;

   first_level = texture->base.u.tex.first_level;
   last_level = texture->base.u.tex.last_level;
   if (last_level - first_level >= LIMA_MAX_MIP_LEVELS)
      last_level = first_level + LIMA_MAX_MIP_LEVELS - 1;

   switch (sampler->base.min_mip_filter) {
      case PIPE_TEX_MIPFILTER_LINEAR:
         desc[2] |= 0x0600;
      case PIPE_TEX_MIPFILTER_NEAREST:
         mipmapping = true;
         desc[1] |= ((last_level - first_level) << 24);
         break;
      case PIPE_TEX_MIPFILTER_NONE:
      default:
         mipmapping = false;
         break;
   }

   switch (sampler->base.mag_img_filter) {
   case PIPE_TEX_FILTER_LINEAR:
      desc[2] &= ~0x1000;
      /* no mipmap, filter_mag = linear */
      if (!mipmapping)
         desc[1] |= 0x80000000;
      break;
   case PIPE_TEX_FILTER_NEAREST:
   default:
      desc[2] |= 0x1000;
      break;
   }

   switch (sampler->base.min_img_filter) {
      break;
   case PIPE_TEX_FILTER_LINEAR:
      desc[2] &= ~0x0800;
      break;
   case PIPE_TEX_FILTER_NEAREST:
   default:
      desc[2] |= 0x0800;
      break;
   }

   /* Only clamp, clamp to edge, repeat and mirror repeat are supported */
   desc[2] &= ~0xe000;
   switch (sampler->base.wrap_s) {
   case PIPE_TEX_WRAP_CLAMP:
      desc[2] |= 0x4000;
      break;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      desc[2] |= 0x2000;
      break;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      desc[2] |= 0x8000;
      break;
   case PIPE_TEX_WRAP_REPEAT:
   default:
      break;
   }

   /* Only clamp, clamp to edge, repeat and mirror repeat are supported */
   desc[2] &= ~0x070000;
   switch (sampler->base.wrap_t) {
   case PIPE_TEX_WRAP_CLAMP:
      desc[2] |= 0x020000;
      break;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      desc[2] |= 0x010000;
      break;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      desc[2] |= 0x040000;
      break;
   case PIPE_TEX_WRAP_REPEAT:
   default:
      break;
   }

   lima_texture_desc_set_res(ctx, desc, texture->base.texture,
                             first_level, last_level);
}

void
lima_update_textures(struct lima_context *ctx)
{
   struct lima_texture_stateobj *lima_tex = &ctx->tex_stateobj;

   assert (lima_tex->num_samplers <= 16);

   /* Nothing to do - we have no samplers or textures */
   if (!lima_tex->num_samplers || !lima_tex->num_textures)
      return;

   unsigned size = lima_tex_list_size + lima_tex->num_samplers * lima_tex_desc_size;
   uint32_t *descs =
      lima_ctx_buff_alloc(ctx, lima_ctx_buff_pp_tex_desc, size, true);

   for (int i = 0; i < lima_tex->num_samplers; i++) {
      off_t offset = lima_tex_desc_size * i + lima_tex_list_size;
      struct lima_sampler_state *sampler = lima_sampler_state(lima_tex->samplers[i]);
      struct lima_sampler_view *texture = lima_sampler_view(lima_tex->textures[i]);

      descs[i] = lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc,
                                  LIMA_CTX_BUFF_SUBMIT_PP) + offset;
      lima_update_tex_desc(ctx, sampler, texture, (void *)descs + offset);
   }

   lima_dump_command_stream_print(
      descs, size, false, "add textures_desc at va %x\n",
      lima_ctx_buff_va(ctx, lima_ctx_buff_pp_tex_desc, 0));
}
