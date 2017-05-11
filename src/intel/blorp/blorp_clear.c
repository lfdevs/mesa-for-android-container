/*
 * Copyright © 2013 Intel Corporation
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

#include "util/ralloc.h"

#include "main/macros.h" /* Needed for MAX3 and MAX2 for format_rgb9e5 */
#include "util/format_rgb9e5.h"

#include "blorp_priv.h"
#include "compiler/brw_eu_defines.h"

#include "compiler/nir/nir_builder.h"

#define FILE_DEBUG_FLAG DEBUG_BLORP

struct brw_blorp_const_color_prog_key
{
   enum blorp_shader_type shader_type; /* Must be BLORP_SHADER_TYPE_CLEAR */
   bool use_simd16_replicated_data;
   bool pad[3];
};

static bool
blorp_params_get_clear_kernel(struct blorp_context *blorp,
                              struct blorp_params *params,
                              bool use_replicated_data)
{
   const struct brw_blorp_const_color_prog_key blorp_key = {
      .shader_type = BLORP_SHADER_TYPE_CLEAR,
      .use_simd16_replicated_data = use_replicated_data,
   };

   if (blorp->lookup_shader(blorp, &blorp_key, sizeof(blorp_key),
                            &params->wm_prog_kernel, &params->wm_prog_data))
      return true;

   void *mem_ctx = ralloc_context(NULL);

   nir_builder b;
   nir_builder_init_simple_shader(&b, mem_ctx, MESA_SHADER_FRAGMENT, NULL);
   b.shader->info->name = ralloc_strdup(b.shader, "BLORP-clear");

   nir_variable *v_color =
      BLORP_CREATE_NIR_INPUT(b.shader, clear_color, glsl_vec4_type());

   nir_variable *frag_color = nir_variable_create(b.shader, nir_var_shader_out,
                                                  glsl_vec4_type(),
                                                  "gl_FragColor");
   frag_color->data.location = FRAG_RESULT_COLOR;

   nir_copy_var(&b, frag_color, v_color);

   struct brw_wm_prog_key wm_key;
   brw_blorp_init_wm_prog_key(&wm_key);

   struct brw_wm_prog_data prog_data;
   unsigned program_size;
   const unsigned *program =
      blorp_compile_fs(blorp, mem_ctx, b.shader, &wm_key, use_replicated_data,
                       &prog_data, &program_size);

   bool result =
      blorp->upload_shader(blorp, &blorp_key, sizeof(blorp_key),
                           program, program_size,
                           &prog_data.base, sizeof(prog_data),
                           &params->wm_prog_kernel, &params->wm_prog_data);

   ralloc_free(mem_ctx);
   return result;
}

struct layer_offset_vs_key {
   enum blorp_shader_type shader_type;
   unsigned num_inputs;
};

/* In the case of doing attachment clears, we are using a surface state that
 * is handed to us so we can't set (and don't even know) the base array layer.
 * In order to do a layered clear in this scenario, we need some way of adding
 * the base array layer to the instance id.  Unfortunately, our hardware has
 * no real concept of "base instance", so we have to do it manually in a
 * vertex shader.
 */
static bool
blorp_params_get_layer_offset_vs(struct blorp_context *blorp,
                                 struct blorp_params *params)
{
   struct layer_offset_vs_key blorp_key = {
      .shader_type = BLORP_SHADER_TYPE_LAYER_OFFSET_VS,
   };

   if (params->wm_prog_data)
      blorp_key.num_inputs = params->wm_prog_data->num_varying_inputs;

   if (blorp->lookup_shader(blorp, &blorp_key, sizeof(blorp_key),
                            &params->vs_prog_kernel, &params->vs_prog_data))
      return true;

   void *mem_ctx = ralloc_context(NULL);

   nir_builder b;
   nir_builder_init_simple_shader(&b, mem_ctx, MESA_SHADER_VERTEX, NULL);
   b.shader->info->name = ralloc_strdup(b.shader, "BLORP-layer-offset-vs");

   const struct glsl_type *uvec4_type = glsl_vector_type(GLSL_TYPE_UINT, 4);

   /* First we deal with the header which has instance and base instance */
   nir_variable *a_header = nir_variable_create(b.shader, nir_var_shader_in,
                                                uvec4_type, "header");
   a_header->data.location = VERT_ATTRIB_GENERIC0;

   nir_variable *v_layer = nir_variable_create(b.shader, nir_var_shader_out,
                                               glsl_int_type(), "layer_id");
   v_layer->data.location = VARYING_SLOT_LAYER;

   /* Compute the layer id */
   nir_ssa_def *header = nir_load_var(&b, a_header);
   nir_ssa_def *base_layer = nir_channel(&b, header, 0);
   nir_ssa_def *instance = nir_channel(&b, header, 1);
   nir_store_var(&b, v_layer, nir_iadd(&b, instance, base_layer), 0x1);

   /* Then we copy the vertex from the next slot to VARYING_SLOT_POS */
   nir_variable *a_vertex = nir_variable_create(b.shader, nir_var_shader_in,
                                                glsl_vec4_type(), "a_vertex");
   a_vertex->data.location = VERT_ATTRIB_GENERIC1;

   nir_variable *v_pos = nir_variable_create(b.shader, nir_var_shader_out,
                                             glsl_vec4_type(), "v_pos");
   v_pos->data.location = VARYING_SLOT_POS;

   nir_copy_var(&b, v_pos, a_vertex);

   /* Then we copy everything else */
   for (unsigned i = 0; i < blorp_key.num_inputs; i++) {
      nir_variable *a_in = nir_variable_create(b.shader, nir_var_shader_in,
                                               uvec4_type, "input");
      a_in->data.location = VERT_ATTRIB_GENERIC2 + i;

      nir_variable *v_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                uvec4_type, "output");
      v_out->data.location = VARYING_SLOT_VAR0 + i;

      nir_copy_var(&b, v_out, a_in);
   }

   struct brw_vs_prog_data vs_prog_data;
   memset(&vs_prog_data, 0, sizeof(vs_prog_data));

   unsigned program_size;
   const unsigned *program =
      blorp_compile_vs(blorp, mem_ctx, b.shader, &vs_prog_data, &program_size);

   bool result =
      blorp->upload_shader(blorp, &blorp_key, sizeof(blorp_key),
                           program, program_size,
                           &vs_prog_data.base.base, sizeof(vs_prog_data),
                           &params->vs_prog_kernel, &params->vs_prog_data);

   ralloc_free(mem_ctx);
   return result;
}

/* The x0, y0, x1, and y1 parameters must already be populated with the render
 * area of the framebuffer to be cleared.
 */
static void
get_fast_clear_rect(const struct isl_device *dev,
                    const struct isl_surf *aux_surf,
                    unsigned *x0, unsigned *y0,
                    unsigned *x1, unsigned *y1)
{
   unsigned int x_align, y_align;
   unsigned int x_scaledown, y_scaledown;

   /* Only single sampled surfaces need to (and actually can) be resolved. */
   if (aux_surf->usage == ISL_SURF_USAGE_CCS_BIT) {
      /* From the Ivy Bridge PRM, Vol2 Part1 11.7 "MCS Buffer for Render
       * Target(s)", beneath the "Fast Color Clear" bullet (p327):
       *
       *     Clear pass must have a clear rectangle that must follow
       *     alignment rules in terms of pixels and lines as shown in the
       *     table below. Further, the clear-rectangle height and width
       *     must be multiple of the following dimensions. If the height
       *     and width of the render target being cleared do not meet these
       *     requirements, an MCS buffer can be created such that it
       *     follows the requirement and covers the RT.
       *
       * The alignment size in the table that follows is related to the
       * alignment size that is baked into the CCS surface format but with X
       * alignment multiplied by 16 and Y alignment multiplied by 32.
       */
      x_align = isl_format_get_layout(aux_surf->format)->bw;
      y_align = isl_format_get_layout(aux_surf->format)->bh;

      x_align *= 16;

      /* SKL+ line alignment requirement for Y-tiled are half those of the prior
       * generations.
       */
      if (dev->info->gen >= 9)
         y_align *= 16;
      else
         y_align *= 32;

      /* From the Ivy Bridge PRM, Vol2 Part1 11.7 "MCS Buffer for Render
       * Target(s)", beneath the "Fast Color Clear" bullet (p327):
       *
       *     In order to optimize the performance MCS buffer (when bound to
       *     1X RT) clear similarly to MCS buffer clear for MSRT case,
       *     clear rect is required to be scaled by the following factors
       *     in the horizontal and vertical directions:
       *
       * The X and Y scale down factors in the table that follows are each
       * equal to half the alignment value computed above.
       */
      x_scaledown = x_align / 2;
      y_scaledown = y_align / 2;

      /* From BSpec: 3D-Media-GPGPU Engine > 3D Pipeline > Pixel > Pixel
       * Backend > MCS Buffer for Render Target(s) [DevIVB+] > Table "Color
       * Clear of Non-MultiSampled Render Target Restrictions":
       *
       *   Clear rectangle must be aligned to two times the number of
       *   pixels in the table shown below due to 16x16 hashing across the
       *   slice.
       */
      x_align *= 2;
      y_align *= 2;
   } else {
      assert(aux_surf->usage == ISL_SURF_USAGE_MCS_BIT);

      /* From the Ivy Bridge PRM, Vol2 Part1 11.7 "MCS Buffer for Render
       * Target(s)", beneath the "MSAA Compression" bullet (p326):
       *
       *     Clear pass for this case requires that scaled down primitive
       *     is sent down with upper left co-ordinate to coincide with
       *     actual rectangle being cleared. For MSAA, clear rectangle’s
       *     height and width need to as show in the following table in
       *     terms of (width,height) of the RT.
       *
       *     MSAA  Width of Clear Rect  Height of Clear Rect
       *      2X     Ceil(1/8*width)      Ceil(1/2*height)
       *      4X     Ceil(1/8*width)      Ceil(1/2*height)
       *      8X     Ceil(1/2*width)      Ceil(1/2*height)
       *     16X         width            Ceil(1/2*height)
       *
       * The text "with upper left co-ordinate to coincide with actual
       * rectangle being cleared" is a little confusing--it seems to imply
       * that to clear a rectangle from (x,y) to (x+w,y+h), one needs to
       * feed the pipeline using the rectangle (x,y) to
       * (x+Ceil(w/N),y+Ceil(h/2)), where N is either 2 or 8 depending on
       * the number of samples.  Experiments indicate that this is not
       * quite correct; actually, what the hardware appears to do is to
       * align whatever rectangle is sent down the pipeline to the nearest
       * multiple of 2x2 blocks, and then scale it up by a factor of N
       * horizontally and 2 vertically.  So the resulting alignment is 4
       * vertically and either 4 or 16 horizontally, and the scaledown
       * factor is 2 vertically and either 2 or 8 horizontally.
       */
      switch (aux_surf->format) {
      case ISL_FORMAT_MCS_2X:
      case ISL_FORMAT_MCS_4X:
         x_scaledown = 8;
         break;
      case ISL_FORMAT_MCS_8X:
         x_scaledown = 2;
         break;
      case ISL_FORMAT_MCS_16X:
         x_scaledown = 1;
         break;
      default:
         unreachable("Unexpected MCS format for fast clear");
      }
      y_scaledown = 2;
      x_align = x_scaledown * 2;
      y_align = y_scaledown * 2;
   }

   *x0 = ROUND_DOWN_TO(*x0,  x_align) / x_scaledown;
   *y0 = ROUND_DOWN_TO(*y0, y_align) / y_scaledown;
   *x1 = ALIGN(*x1, x_align) / x_scaledown;
   *y1 = ALIGN(*y1, y_align) / y_scaledown;
}

void
blorp_fast_clear(struct blorp_batch *batch,
                 const struct blorp_surf *surf, enum isl_format format,
                 uint32_t level, uint32_t start_layer, uint32_t num_layers,
                 uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
   struct blorp_params params;
   blorp_params_init(&params);
   params.num_layers = num_layers;

   params.x0 = x0;
   params.y0 = y0;
   params.x1 = x1;
   params.y1 = y1;

   memset(&params.wm_inputs.clear_color, 0xff, 4*sizeof(float));
   params.fast_clear_op = BLORP_FAST_CLEAR_OP_CLEAR;

   get_fast_clear_rect(batch->blorp->isl_dev, surf->aux_surf,
                       &params.x0, &params.y0, &params.x1, &params.y1);

   if (!blorp_params_get_clear_kernel(batch->blorp, &params, true))
      return;

   brw_blorp_surface_info_init(batch->blorp, &params.dst, surf, level,
                               start_layer, format, true);
   params.num_samples = params.dst.surf.samples;

   batch->blorp->exec(batch, &params);
}

static union isl_color_value
swizzle_color_value(union isl_color_value src, struct isl_swizzle swizzle)
{
   union isl_color_value dst = { .u32 = { 0, } };

   /* We assign colors in ABGR order so that the first one will be taken in
    * RGBA precedence order.  According to the PRM docs for shader channel
    * select, this matches Haswell hardware behavior.
    */
   if ((unsigned)(swizzle.a - ISL_CHANNEL_SELECT_RED) < 4)
      dst.u32[swizzle.a - ISL_CHANNEL_SELECT_RED] = src.u32[3];
   if ((unsigned)(swizzle.b - ISL_CHANNEL_SELECT_RED) < 4)
      dst.u32[swizzle.b - ISL_CHANNEL_SELECT_RED] = src.u32[2];
   if ((unsigned)(swizzle.g - ISL_CHANNEL_SELECT_RED) < 4)
      dst.u32[swizzle.g - ISL_CHANNEL_SELECT_RED] = src.u32[1];
   if ((unsigned)(swizzle.r - ISL_CHANNEL_SELECT_RED) < 4)
      dst.u32[swizzle.r - ISL_CHANNEL_SELECT_RED] = src.u32[0];

   return dst;
}

void
blorp_clear(struct blorp_batch *batch,
            const struct blorp_surf *surf,
            enum isl_format format, struct isl_swizzle swizzle,
            uint32_t level, uint32_t start_layer, uint32_t num_layers,
            uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
            union isl_color_value clear_color,
            const bool color_write_disable[4])
{
   struct blorp_params params;
   blorp_params_init(&params);

   params.x0 = x0;
   params.y0 = y0;
   params.x1 = x1;
   params.y1 = y1;

   /* Manually apply the clear destination swizzle.  This way swizzled clears
    * will work for swizzles which we can't normally use for rendering and it
    * also ensures that they work on pre-Haswell hardware which can't swizlle
    * at all.
    */
   clear_color = swizzle_color_value(clear_color, swizzle);
   swizzle = ISL_SWIZZLE_IDENTITY;

   if (format == ISL_FORMAT_R9G9B9E5_SHAREDEXP) {
      clear_color.u32[0] = float3_to_rgb9e5(clear_color.f32);
      format = ISL_FORMAT_R32_UINT;
   } else if (format == ISL_FORMAT_A4B4G4R4_UNORM) {
      /* Broadwell and earlier cannot render to this format so we need to work
       * around it by swapping the colors around and using B4G4R4A4 instead.
       */
      const struct isl_swizzle ARGB = ISL_SWIZZLE(ALPHA, RED, GREEN, BLUE);
      clear_color = swizzle_color_value(clear_color, ARGB);
      format = ISL_FORMAT_B4G4R4A4_UNORM;
   }

   memcpy(&params.wm_inputs.clear_color, clear_color.f32, sizeof(float) * 4);

   bool use_simd16_replicated_data = true;

   /* From the SNB PRM (Vol4_Part1):
    *
    *     "Replicated data (Message Type = 111) is only supported when
    *      accessing tiled memory.  Using this Message Type to access linear
    *      (untiled) memory is UNDEFINED."
    */
   if (surf->surf->tiling == ISL_TILING_LINEAR)
      use_simd16_replicated_data = false;

   /* Constant color writes ignore everyting in blend and color calculator
    * state.  This is not documented.
    */
   if (color_write_disable) {
      for (unsigned i = 0; i < 4; i++) {
         params.color_write_disable[i] = color_write_disable[i];
         if (color_write_disable[i])
            use_simd16_replicated_data = false;
      }
   }

   if (!blorp_params_get_clear_kernel(batch->blorp, &params,
                                      use_simd16_replicated_data))
      return;

   while (num_layers > 0) {
      brw_blorp_surface_info_init(batch->blorp, &params.dst, surf, level,
                                  start_layer, format, true);
      params.dst.view.swizzle = swizzle;

      params.num_samples = params.dst.surf.samples;

      /* We may be restricted on the number of layers we can bind at any one
       * time.  In particular, Sandy Bridge has a maximum number of layers of
       * 512 but a maximum 3D texture size is much larger.
       */
      params.num_layers = MIN2(params.dst.view.array_len, num_layers);
      batch->blorp->exec(batch, &params);

      start_layer += params.num_layers;
      num_layers -= params.num_layers;
   }
}

void
blorp_clear_depth_stencil(struct blorp_batch *batch,
                          const struct blorp_surf *depth,
                          const struct blorp_surf *stencil,
                          uint32_t level, uint32_t start_layer,
                          uint32_t num_layers,
                          uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                          bool clear_depth, float depth_value,
                          uint8_t stencil_mask, uint8_t stencil_value)
{
   struct blorp_params params;
   blorp_params_init(&params);

   params.x0 = x0;
   params.y0 = y0;
   params.x1 = x1;
   params.y1 = y1;

   while (num_layers > 0) {
      params.num_layers = num_layers;

      if (stencil_mask) {
         brw_blorp_surface_info_init(batch->blorp, &params.stencil, stencil,
                                     level, start_layer,
                                     ISL_FORMAT_UNSUPPORTED, true);
         params.stencil_mask = stencil_mask;
         params.stencil_ref = stencil_value;

         params.dst.surf.samples = params.stencil.surf.samples;
         params.dst.surf.logical_level0_px =
            params.stencil.surf.logical_level0_px;
         params.dst.view = params.depth.view;

         params.num_samples = params.stencil.surf.samples;

         /* We may be restricted on the number of layers we can bind at any
          * one time.  In particular, Sandy Bridge has a maximum number of
          * layers of 512 but a maximum 3D texture size is much larger.
          */
         if (params.stencil.view.array_len < params.num_layers)
            params.num_layers = params.stencil.view.array_len;
      }

      if (clear_depth) {
         brw_blorp_surface_info_init(batch->blorp, &params.depth, depth,
                                     level, start_layer,
                                     ISL_FORMAT_UNSUPPORTED, true);
         params.z = depth_value;
         params.depth_format =
            isl_format_get_depth_format(depth->surf->format, false);

         params.dst.surf.samples = params.depth.surf.samples;
         params.dst.surf.logical_level0_px =
            params.depth.surf.logical_level0_px;
         params.dst.view = params.depth.view;

         params.num_samples = params.depth.surf.samples;

         /* We may be restricted on the number of layers we can bind at any
          * one time.  In particular, Sandy Bridge has a maximum number of
          * layers of 512 but a maximum 3D texture size is much larger.
          */
         if (params.depth.view.array_len < params.num_layers)
            params.num_layers = params.depth.view.array_len;
      }

      batch->blorp->exec(batch, &params);

      start_layer += params.num_layers;
      num_layers -= params.num_layers;
   }
}

bool
blorp_can_hiz_clear_depth(uint8_t gen, enum isl_format format,
                          uint32_t num_samples,
                          uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
   /* This function currently doesn't support any gen prior to gen8 */
   assert(gen >= 8);

   if (gen == 8 && format == ISL_FORMAT_R16_UNORM) {
      /* Apply the D16 alignment restrictions. On BDW, HiZ has an 8x4 sample
       * block with the following property: as the number of samples increases,
       * the number of pixels representable by this block decreases by a factor
       * of the sample dimensions. Sample dimensions scale following the MSAA
       * interleaved pattern.
       *
       * Sample|Sample|Pixel
       * Count |Dim   |Dim
       * ===================
       *    1  | 1x1  | 8x4
       *    2  | 2x1  | 4x4
       *    4  | 2x2  | 4x2
       *    8  | 4x2  | 2x2
       *   16  | 4x4  | 2x1
       *
       * Table: Pixel Dimensions in a HiZ Sample Block Pre-SKL
       */
      const struct isl_extent2d sa_block_dim =
         isl_get_interleaved_msaa_px_size_sa(num_samples);
      const uint8_t align_px_w = 8 / sa_block_dim.w;
      const uint8_t align_px_h = 4 / sa_block_dim.h;

      /* Fast depth clears clear an entire sample block at a time. As a result,
       * the rectangle must be aligned to the dimensions of the encompassing
       * pixel block for a successful operation.
       *
       * Fast clears can still work if the upper-left corner is aligned and the
       * bottom-rigtht corner touches the edge of a depth buffer whose extent
       * is unaligned. This is because each miplevel in the depth buffer is
       * padded by the Pixel Dim (similar to a standard compressed texture).
       * In this case, the clear rectangle could be padded by to match the full
       * depth buffer extent but to support multiple clearing techniques, we
       * chose to be unaware of the depth buffer's extent and thus don't handle
       * this case.
       */
      if (x0 % align_px_w || y0 % align_px_h ||
          x1 % align_px_w || y1 % align_px_h)
         return false;
   }
   return true;
}

/* Given a depth stencil attachment, this function performs a fast depth clear
 * on a depth portion and a regular clear on the stencil portion. When
 * performing a fast depth clear on the depth portion, the HiZ buffer is simply
 * tagged as cleared so the depth clear value is not actually needed.
 */
void
blorp_gen8_hiz_clear_attachments(struct blorp_batch *batch,
                                 uint32_t num_samples,
                                 uint32_t x0, uint32_t y0,
                                 uint32_t x1, uint32_t y1,
                                 bool clear_depth, bool clear_stencil,
                                 uint8_t stencil_value)
{
   assert(batch->flags & BLORP_BATCH_NO_EMIT_DEPTH_STENCIL);

   struct blorp_params params;
   blorp_params_init(&params);
   params.num_layers = 1;
   params.hiz_op = BLORP_HIZ_OP_DEPTH_CLEAR;
   params.x0 = x0;
   params.y0 = y0;
   params.x1 = x1;
   params.y1 = y1;
   params.num_samples = num_samples;
   params.depth.enabled = clear_depth;
   params.stencil.enabled = clear_stencil;
   params.stencil_ref = stencil_value;
   batch->blorp->exec(batch, &params);
}

/** Clear active color/depth/stencili attachments
 *
 * This function performs a clear operation on the currently bound
 * color/depth/stencil attachments.  It is assumed that any information passed
 * in here is valid, consistent, and in-bounds relative to the currently
 * attached depth/stencil.  The binding_table_offset parameter is the 32-bit
 * offset relative to surface state base address where pre-baked binding table
 * that we are to use lives.  If clear_color is false, binding_table_offset
 * must point to a binding table with one entry which is a valid null surface
 * that matches the currently bound depth and stencil.
 */
void
blorp_clear_attachments(struct blorp_batch *batch,
                        uint32_t binding_table_offset,
                        enum isl_format depth_format,
                        uint32_t num_samples,
                        uint32_t start_layer, uint32_t num_layers,
                        uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                        bool clear_color, union isl_color_value color_value,
                        bool clear_depth, float depth_value,
                        uint8_t stencil_mask, uint8_t stencil_value)
{
   struct blorp_params params;
   blorp_params_init(&params);

   assert(batch->flags & BLORP_BATCH_NO_EMIT_DEPTH_STENCIL);

   params.x0 = x0;
   params.y0 = y0;
   params.x1 = x1;
   params.y1 = y1;

   params.use_pre_baked_binding_table = true;
   params.pre_baked_binding_table_offset = binding_table_offset;

   params.num_layers = num_layers;
   params.num_samples = num_samples;

   if (clear_color) {
      params.dst.enabled = true;

      memcpy(&params.wm_inputs.clear_color, color_value.f32, sizeof(float) * 4);

      /* Unfortunately, without knowing whether or not our destination surface
       * is tiled or not, we have to assume it may be linear.  This means no
       * SIMD16_REPDATA for us. :-(
       */
      if (!blorp_params_get_clear_kernel(batch->blorp, &params, false))
         return;
   }

   if (clear_depth) {
      params.depth.enabled = true;

      params.z = depth_value;
      params.depth_format = isl_format_get_depth_format(depth_format, false);
   }

   if (stencil_mask) {
      params.stencil.enabled = true;

      params.stencil_mask = stencil_mask;
      params.stencil_ref = stencil_value;
   }

   if (!blorp_params_get_layer_offset_vs(batch->blorp, &params))
      return;

   params.vs_inputs.base_layer = start_layer;

   batch->blorp->exec(batch, &params);
}

void
blorp_ccs_resolve(struct blorp_batch *batch,
                  struct blorp_surf *surf, uint32_t level, uint32_t layer,
                  enum isl_format format,
                  enum blorp_fast_clear_op resolve_op)
{
   struct blorp_params params;
   blorp_params_init(&params);

   /* Layered and mipmapped fast clear is only available from Gen8 onwards. */
   assert(ISL_DEV_GEN(batch->blorp->isl_dev) >= 8 ||
          (level == 0 && layer == 0));

   brw_blorp_surface_info_init(batch->blorp, &params.dst, surf,
                               level, layer, format, true);

   /* From the Ivy Bridge PRM, Vol2 Part1 11.9 "Render Target Resolve":
    *
    *     A rectangle primitive must be scaled down by the following factors
    *     with respect to render target being resolved.
    *
    * The scaledown factors in the table that follows are related to the block
    * size of the CCS format.  For IVB and HSW, we divide by two, for BDW we
    * multiply by 8 and 16. On Sky Lake, we multiply by 8.
    */
   const struct isl_format_layout *aux_fmtl =
      isl_format_get_layout(params.dst.aux_surf.format);
   assert(aux_fmtl->txc == ISL_TXC_CCS);

   unsigned x_scaledown, y_scaledown;
   if (ISL_DEV_GEN(batch->blorp->isl_dev) >= 9) {
      x_scaledown = aux_fmtl->bw * 8;
      y_scaledown = aux_fmtl->bh * 8;
   } else if (ISL_DEV_GEN(batch->blorp->isl_dev) >= 8) {
      x_scaledown = aux_fmtl->bw * 8;
      y_scaledown = aux_fmtl->bh * 16;
   } else {
      x_scaledown = aux_fmtl->bw / 2;
      y_scaledown = aux_fmtl->bh / 2;
   }
   params.x0 = params.y0 = 0;
   params.x1 = minify(params.dst.aux_surf.logical_level0_px.width, level);
   params.y1 = minify(params.dst.aux_surf.logical_level0_px.height, level);
   params.x1 = ALIGN(params.x1, x_scaledown) / x_scaledown;
   params.y1 = ALIGN(params.y1, y_scaledown) / y_scaledown;

   if (batch->blorp->isl_dev->info->gen >= 9) {
      assert(resolve_op == BLORP_FAST_CLEAR_OP_RESOLVE_FULL ||
             resolve_op == BLORP_FAST_CLEAR_OP_RESOLVE_PARTIAL);
   } else {
      /* Broadwell and earlier do not have a partial resolve */
      assert(resolve_op == BLORP_FAST_CLEAR_OP_RESOLVE_FULL);
   }
   params.fast_clear_op = resolve_op;

   /* Note: there is no need to initialize push constants because it doesn't
    * matter what data gets dispatched to the render target.  However, we must
    * ensure that the fragment shader delivers the data using the "replicated
    * color" message.
    */

   if (!blorp_params_get_clear_kernel(batch->blorp, &params, true))
      return;

   batch->blorp->exec(batch, &params);
}
