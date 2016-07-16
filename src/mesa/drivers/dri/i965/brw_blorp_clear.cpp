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

#include "main/teximage.h"
#include "main/blend.h"
#include "main/fbobject.h"
#include "main/renderbuffer.h"
#include "main/glformats.h"

#include "util/ralloc.h"

#include "intel_fbo.h"

#include "brw_blorp.h"
#include "brw_meta_util.h"
#include "brw_context.h"
#include "brw_eu.h"
#include "brw_state.h"

#include "nir_builder.h"

#define FILE_DEBUG_FLAG DEBUG_BLORP

struct brw_blorp_const_color_prog_key
{
   bool use_simd16_replicated_data;
   bool pad[3];
};

static void
brw_blorp_params_get_clear_kernel(struct brw_context *brw,
                                  struct brw_blorp_params *params,
                                  bool use_replicated_data)
{
   struct brw_blorp_const_color_prog_key blorp_key;
   memset(&blorp_key, 0, sizeof(blorp_key));
   blorp_key.use_simd16_replicated_data = use_replicated_data;

   if (brw_search_cache(&brw->cache, BRW_CACHE_BLORP_PROG,
                        &blorp_key, sizeof(blorp_key),
                        &params->wm_prog_kernel, &params->wm_prog_data))
      return;

   void *mem_ctx = ralloc_context(NULL);

   nir_builder b;
   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "BLORP-clear");

   nir_variable *u_color = nir_variable_create(b.shader, nir_var_uniform,
                                               glsl_vec4_type(), "u_color");
   u_color->data.location = 0;

   nir_variable *frag_color = nir_variable_create(b.shader, nir_var_shader_out,
                                                  glsl_vec4_type(),
                                                  "gl_FragColor");
   frag_color->data.location = FRAG_RESULT_COLOR;

   nir_copy_var(&b, frag_color, u_color);

   struct brw_wm_prog_key wm_key;
   brw_blorp_init_wm_prog_key(&wm_key);

   struct brw_blorp_prog_data prog_data;
   unsigned program_size;
   const unsigned *program =
      brw_blorp_compile_nir_shader(brw, b.shader, &wm_key, use_replicated_data,
                                   &prog_data, &program_size);

   brw_upload_cache(&brw->cache, BRW_CACHE_BLORP_PROG,
                    &blorp_key, sizeof(blorp_key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &params->wm_prog_kernel, &params->wm_prog_data);

   ralloc_free(mem_ctx);
}

static bool
set_write_disables(const struct intel_renderbuffer *irb, 
                   const GLubyte *color_mask, bool *color_write_disable)
{
   /* Format information in the renderbuffer represents the requirements
    * given by the client. There are cases where the backing miptree uses,
    * for example, RGBA to represent RGBX. Since the client is only expecting
    * RGB we can treat alpha as not used and write whatever we like into it.
    */
   const GLenum base_format = irb->Base.Base._BaseFormat;
   const int components = _mesa_base_format_component_count(base_format);
   bool disables = false;

   assert(components > 0);

   for (int i = 0; i < components; i++) {
      color_write_disable[i] = !color_mask[i];
      disables = disables || !color_mask[i];
   }

   return disables;
}

static bool
do_single_blorp_clear(struct brw_context *brw, struct gl_framebuffer *fb,
                      struct gl_renderbuffer *rb, unsigned buf,
                      bool partial_clear, bool encode_srgb, unsigned layer)
{
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);
   mesa_format format = irb->mt->format;

   struct brw_blorp_params params;
   brw_blorp_params_init(&params);

   if (!encode_srgb && _mesa_get_format_color_encoding(format) == GL_SRGB)
      format = _mesa_get_srgb_format_linear(format);

   brw_blorp_surface_info_init(brw, &params.dst, irb->mt, irb->mt_level,
                               layer, format, true);

   /* Override the surface format according to the context's sRGB rules. */
   params.dst.brw_surfaceformat = brw->render_target_format[format];

   params.x0 = fb->_Xmin;
   params.x1 = fb->_Xmax;
   if (rb->Name != 0) {
      params.y0 = fb->_Ymin;
      params.y1 = fb->_Ymax;
   } else {
      params.y0 = rb->Height - fb->_Ymax;
      params.y1 = rb->Height - fb->_Ymin;
   }

   memcpy(&params.wm_push_consts.dst_x0,
          ctx->Color.ClearColor.f, sizeof(float) * 4);

   bool use_simd16_replicated_data = true;

   /* From the SNB PRM (Vol4_Part1):
    *
    *     "Replicated data (Message Type = 111) is only supported when
    *      accessing tiled memory.  Using this Message Type to access linear
    *      (untiled) memory is UNDEFINED."
    */
   if (irb->mt->tiling == I915_TILING_NONE)
      use_simd16_replicated_data = false;

   /* Constant color writes ignore everyting in blend and color calculator
    * state.  This is not documented.
    */
   if (set_write_disables(irb, ctx->Color.ColorMask[buf],
                          params.color_write_disable))
      use_simd16_replicated_data = false;

   if (irb->mt->fast_clear_state != INTEL_FAST_CLEAR_STATE_NO_MCS &&
       !partial_clear && use_simd16_replicated_data &&
       brw_is_color_fast_clear_compatible(brw, irb->mt,
                                          &ctx->Color.ClearColor)) {
      memset(&params.wm_push_consts, 0xff, 4*sizeof(float));
      params.fast_clear_op = GEN7_PS_RENDER_TARGET_FAST_CLEAR_ENABLE;

      brw_get_fast_clear_rect(brw, fb, irb->mt, &params.x0, &params.y0,
                              &params.x1, &params.y1);
   } else {
      brw_meta_get_buffer_rect(fb, &params.x0, &params.y0,
                               &params.x1, &params.y1);
   }

   brw_blorp_params_get_clear_kernel(brw, &params, use_simd16_replicated_data);

   const bool is_fast_clear =
      params.fast_clear_op == GEN7_PS_RENDER_TARGET_FAST_CLEAR_ENABLE;
   if (is_fast_clear) {
      /* Record the clear color in the miptree so that it will be
       * programmed in SURFACE_STATE by later rendering and resolve
       * operations.
       */
      const bool color_updated = brw_meta_set_fast_clear_color(
                                    brw, irb->mt, &ctx->Color.ClearColor);

      /* If the buffer is already in INTEL_FAST_CLEAR_STATE_CLEAR, the clear
       * is redundant and can be skipped.
       */
      if (!color_updated &&
          irb->mt->fast_clear_state == INTEL_FAST_CLEAR_STATE_CLEAR)
         return true;

      /* If the MCS buffer hasn't been allocated yet, we need to allocate
       * it now.
       */
      if (!irb->mt->mcs_mt) {
         if (!intel_miptree_alloc_non_msrt_mcs(brw, irb->mt)) {
            /* MCS allocation failed--probably this will only happen in
             * out-of-memory conditions.  But in any case, try to recover
             * by falling back to a non-blorp clear technique.
             */
            return false;
         }
      }
   }

   const char *clear_type;
   if (is_fast_clear)
      clear_type = "fast";
   else if (use_simd16_replicated_data)
      clear_type = "replicated";
   else
      clear_type = "slow";

   DBG("%s (%s) to mt %p level %d layer %d\n", __FUNCTION__, clear_type,
       irb->mt, irb->mt_level, irb->mt_layer);

   brw_blorp_exec(brw, &params);

   if (is_fast_clear) {
      /* Now that the fast clear has occurred, put the buffer in
       * INTEL_FAST_CLEAR_STATE_CLEAR so that we won't waste time doing
       * redundant clears.
       */
      irb->mt->fast_clear_state = INTEL_FAST_CLEAR_STATE_CLEAR;
   } else if (intel_miptree_is_lossless_compressed(brw, irb->mt)) {
      /* Compressed buffers can be cleared also using normal rep-clear. In
       * such case they bahave such as if they were drawn using normal 3D
       * render pipeline, and we simply mark the mcs as dirty.
       */
      assert(partial_clear);
      irb->mt->fast_clear_state = INTEL_FAST_CLEAR_STATE_UNRESOLVED;
   }

   return true;
}


extern "C" {
bool
brw_blorp_clear_color(struct brw_context *brw, struct gl_framebuffer *fb,
                      GLbitfield mask, bool partial_clear, bool encode_srgb)
{
   for (unsigned buf = 0; buf < fb->_NumColorDrawBuffers; buf++) {
      struct gl_renderbuffer *rb = fb->_ColorDrawBuffers[buf];
      struct intel_renderbuffer *irb = intel_renderbuffer(rb);

      /* Only clear the buffers present in the provided mask */
      if (((1 << fb->_ColorDrawBufferIndexes[buf]) & mask) == 0)
         continue;

      /* If this is an ES2 context or GL_ARB_ES2_compatibility is supported,
       * the framebuffer can be complete with some attachments missing.  In
       * this case the _ColorDrawBuffers pointer will be NULL.
       */
      if (rb == NULL)
         continue;

      if (fb->MaxNumLayers > 0) {
         unsigned layer_multiplier =
            (irb->mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
             irb->mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS) ?
            irb->mt->num_samples : 1;
         unsigned num_layers = irb->layer_count;
         for (unsigned layer = 0; layer < num_layers; layer++) {
            if (!do_single_blorp_clear(
                    brw, fb, rb, buf, partial_clear, encode_srgb,
                    irb->mt_layer + layer * layer_multiplier)) {
               return false;
            }
         }
      } else {
         unsigned layer = irb->mt_layer;
         if (!do_single_blorp_clear(brw, fb, rb, buf, partial_clear,
                                    encode_srgb, layer))
            return false;
      }

      irb->need_downsample = true;
   }

   return true;
}

void
brw_blorp_resolve_color(struct brw_context *brw, struct intel_mipmap_tree *mt)
{
   DBG("%s to mt %p\n", __FUNCTION__, mt);

   const mesa_format format = _mesa_get_srgb_format_linear(mt->format);

   struct brw_blorp_params params;
   brw_blorp_params_init(&params);

   brw_blorp_surface_info_init(brw, &params.dst, mt,
                               0 /* level */, 0 /* layer */, format, true);

   brw_get_resolve_rect(brw, mt, &params.x0, &params.y0,
                        &params.x1, &params.y1);

   if (intel_miptree_is_lossless_compressed(brw, mt))
      params.resolve_type = GEN9_PS_RENDER_TARGET_RESOLVE_FULL;
   else
      params.resolve_type = GEN7_PS_RENDER_TARGET_RESOLVE_ENABLE;

   /* Note: there is no need to initialize push constants because it doesn't
    * matter what data gets dispatched to the render target.  However, we must
    * ensure that the fragment shader delivers the data using the "replicated
    * color" message.
    */

   brw_blorp_params_get_clear_kernel(brw, &params, true);

   brw_blorp_exec(brw, &params);
   mt->fast_clear_state = INTEL_FAST_CLEAR_STATE_RESOLVED;
}

} /* extern "C" */
