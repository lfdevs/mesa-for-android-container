/*
 * Copyright © 2012 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>

#include "program/prog_instruction.h"

#include "blorp_priv.h"
#include "brw_compiler.h"
#include "brw_nir.h"

void
blorp_init(struct blorp_context *blorp, void *driver_ctx,
           struct isl_device *isl_dev)
{
   blorp->driver_ctx = driver_ctx;
   blorp->isl_dev = isl_dev;
}

void
blorp_finish(struct blorp_context *blorp)
{
   blorp->driver_ctx = NULL;
}

void
blorp_batch_init(struct blorp_context *blorp,
                 struct blorp_batch *batch, void *driver_batch,
                 enum blorp_batch_flags flags)
{
   batch->blorp = blorp;
   batch->driver_batch = driver_batch;
   batch->flags = flags;
}

void
blorp_batch_finish(struct blorp_batch *batch)
{
   batch->blorp = NULL;
}

void
brw_blorp_surface_info_init(struct blorp_context *blorp,
                            struct brw_blorp_surface_info *info,
                            const struct blorp_surf *surf,
                            unsigned int level, unsigned int layer,
                            enum isl_format format, bool is_render_target)
{
   info->enabled = true;

   if (format == ISL_FORMAT_UNSUPPORTED)
      format = surf->surf->format;

   if (format == ISL_FORMAT_R24_UNORM_X8_TYPELESS) {
      /* Unfortunately, ISL_FORMAT_R24_UNORM_X8_TYPELESS it isn't supported as
       * a render target, which would prevent us from blitting to 24-bit
       * depth.  The miptree consists of 32 bits per pixel, arranged as 24-bit
       * depth values interleaved with 8 "don't care" bits.  Since depth
       * values don't require any blending, it doesn't matter how we interpret
       * the bit pattern as long as we copy the right amount of data, so just
       * map it as 8-bit BGRA.
       */
      format = ISL_FORMAT_B8G8R8A8_UNORM;
   } else if (surf->surf->usage & ISL_SURF_USAGE_STENCIL_BIT) {
      assert(surf->surf->format == ISL_FORMAT_R8_UINT);
      /* Prior to Broadwell, we can't render to R8_UINT */
      if (blorp->isl_dev->info->gen < 8)
         format = ISL_FORMAT_R8_UNORM;
   }

   info->surf = *surf->surf;
   info->addr = surf->addr;

   info->aux_usage = surf->aux_usage;
   if (info->aux_usage != ISL_AUX_USAGE_NONE) {
      info->aux_surf = *surf->aux_surf;
      info->aux_addr = surf->aux_addr;
   }

   info->clear_color = surf->clear_color;

   info->view = (struct isl_view) {
      .usage = is_render_target ? ISL_SURF_USAGE_RENDER_TARGET_BIT :
                                  ISL_SURF_USAGE_TEXTURE_BIT,
      .format = format,
      .base_level = level,
      .levels = 1,
      .swizzle = ISL_SWIZZLE_IDENTITY,
   };

   info->view.array_len = MAX2(info->surf.logical_level0_px.depth,
                               info->surf.logical_level0_px.array_len);

   if (!is_render_target &&
       (info->surf.dim == ISL_SURF_DIM_3D ||
        info->surf.msaa_layout == ISL_MSAA_LAYOUT_ARRAY)) {
      /* 3-D textures don't support base_array layer and neither do 2-D
       * multisampled textures on IVB so we need to pass it through the
       * sampler in those cases.  These are also two cases where we are
       * guaranteed that we won't be doing any funny surface hacks.
       */
      info->view.base_array_layer = 0;
      info->z_offset = layer;
   } else {
      info->view.base_array_layer = layer;

      assert(info->view.array_len >= info->view.base_array_layer);
      info->view.array_len -= info->view.base_array_layer;
      info->z_offset = 0;
   }

   /* Sandy Bridge has a limit of a maximum of 512 layers for layered
    * rendering.
    */
   if (is_render_target && blorp->isl_dev->info->gen == 6)
      info->view.array_len = MIN2(info->view.array_len, 512);
}


void
blorp_params_init(struct blorp_params *params)
{
   memset(params, 0, sizeof(*params));
   params->num_samples = 1;
   params->num_draw_buffers = 1;
   params->num_layers = 1;
}

void
brw_blorp_init_wm_prog_key(struct brw_wm_prog_key *wm_key)
{
   memset(wm_key, 0, sizeof(*wm_key));
   wm_key->nr_color_regions = 1;
   for (int i = 0; i < MAX_SAMPLERS; i++)
      wm_key->tex.swizzles[i] = SWIZZLE_XYZW;
}

const unsigned *
blorp_compile_fs(struct blorp_context *blorp, void *mem_ctx,
                 struct nir_shader *nir,
                 const struct brw_wm_prog_key *wm_key,
                 bool use_repclear,
                 struct brw_wm_prog_data *wm_prog_data,
                 unsigned *program_size)
{
   const struct brw_compiler *compiler = blorp->compiler;

   nir->options =
      compiler->glsl_compiler_options[MESA_SHADER_FRAGMENT].NirOptions;

   memset(wm_prog_data, 0, sizeof(*wm_prog_data));

   assert(exec_list_is_empty(&nir->uniforms));
   wm_prog_data->base.nr_params = 0;
   wm_prog_data->base.param = NULL;

   /* BLORP always just uses the first two binding table entries */
   wm_prog_data->binding_table.render_target_start = BLORP_RENDERBUFFER_BT_INDEX;
   wm_prog_data->base.binding_table.texture_start = BLORP_TEXTURE_BT_INDEX;

   nir = brw_preprocess_nir(compiler, nir);
   nir_remove_dead_variables(nir, nir_var_shader_in);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   const unsigned *program =
      brw_compile_fs(compiler, blorp->driver_ctx, mem_ctx, wm_key,
                     wm_prog_data, nir, NULL, -1, -1, false, use_repclear,
                     NULL, program_size, NULL);

   return program;
}

const unsigned *
blorp_compile_vs(struct blorp_context *blorp, void *mem_ctx,
                 struct nir_shader *nir,
                 struct brw_vs_prog_data *vs_prog_data,
                 unsigned *program_size)
{
   const struct brw_compiler *compiler = blorp->compiler;

   nir->options =
      compiler->glsl_compiler_options[MESA_SHADER_VERTEX].NirOptions;

   nir = brw_preprocess_nir(compiler, nir);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   vs_prog_data->inputs_read = nir->info->inputs_read;

   brw_compute_vue_map(compiler->devinfo,
                       &vs_prog_data->base.vue_map,
                       nir->info->outputs_written,
                       nir->info->separate_shader);

   struct brw_vs_prog_key vs_key = { 0, };

   const unsigned *program =
      brw_compile_vs(compiler, blorp->driver_ctx, mem_ctx,
                     &vs_key, vs_prog_data, nir,
                     NULL, false, -1, program_size, NULL);

   return program;
}

void
blorp_gen6_hiz_op(struct blorp_batch *batch,
                  struct blorp_surf *surf, unsigned level, unsigned layer,
                  enum blorp_hiz_op op)
{
   struct blorp_params params;
   blorp_params_init(&params);

   params.hiz_op = op;

   brw_blorp_surface_info_init(batch->blorp, &params.depth, surf, level, layer,
                               surf->surf->format, true);

   /* Align the rectangle primitive to 8x4 pixels.
    *
    * During fast depth clears, the emitted rectangle primitive  must be
    * aligned to 8x4 pixels.  From the Ivybridge PRM, Vol 2 Part 1 Section
    * 11.5.3.1 Depth Buffer Clear (and the matching section in the Sandybridge
    * PRM):
    *     If Number of Multisamples is NUMSAMPLES_1, the rectangle must be
    *     aligned to an 8x4 pixel block relative to the upper left corner
    *     of the depth buffer [...]
    *
    * For hiz resolves, the rectangle must also be 8x4 aligned. Item
    * WaHizAmbiguate8x4Aligned from the Haswell workarounds page and the
    * Ivybridge simulator require the alignment.
    *
    * To be safe, let's just align the rect for all hiz operations and all
    * hardware generations.
    *
    * However, for some miptree slices of a Z24 texture, emitting an 8x4
    * aligned rectangle that covers the slice may clobber adjacent slices if
    * we strictly adhered to the texture alignments specified in the PRM.  The
    * Ivybridge PRM, Section "Alignment Unit Size", states that
    * SURFACE_STATE.Surface_Horizontal_Alignment should be 4 for Z24 surfaces,
    * not 8. But commit 1f112cc increased the alignment from 4 to 8, which
    * prevents the clobbering.
    */
   params.x1 = minify(params.depth.surf.logical_level0_px.width,
                      params.depth.view.base_level);
   params.y1 = minify(params.depth.surf.logical_level0_px.height,
                      params.depth.view.base_level);
   params.x1 = ALIGN(params.x1, 8);
   params.y1 = ALIGN(params.y1, 4);

   if (params.depth.view.base_level == 0) {
      /* TODO: What about MSAA? */
      params.depth.surf.logical_level0_px.width = params.x1;
      params.depth.surf.logical_level0_px.height = params.y1;
   }

   params.dst.surf.samples = params.depth.surf.samples;
   params.dst.surf.logical_level0_px = params.depth.surf.logical_level0_px;
   params.depth_format = isl_format_get_depth_format(surf->surf->format, false);
   params.num_samples = params.depth.surf.samples;

   batch->blorp->exec(batch, &params);
}
