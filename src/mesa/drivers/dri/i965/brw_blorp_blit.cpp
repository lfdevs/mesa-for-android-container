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

#include "main/context.h"
#include "main/teximage.h"
#include "main/fbobject.h"

#include "compiler/nir/nir_builder.h"

#include "intel_fbo.h"

#include "brw_blorp.h"
#include "brw_context.h"
#include "brw_state.h"
#include "brw_meta_util.h"

#define FILE_DEBUG_FLAG DEBUG_BLORP

static struct intel_mipmap_tree *
find_miptree(GLbitfield buffer_bit, struct intel_renderbuffer *irb)
{
   struct intel_mipmap_tree *mt = irb->mt;
   if (buffer_bit == GL_STENCIL_BUFFER_BIT && mt->stencil_mt)
      mt = mt->stencil_mt;
   return mt;
}

static int
blorp_get_texture_swizzle(const struct intel_renderbuffer *irb)
{
   return irb->Base.Base._BaseFormat == GL_RGB ?
      MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_ONE) :
      SWIZZLE_XYZW;
}

static void
do_blorp_blit(struct brw_context *brw, GLbitfield buffer_bit,
              struct intel_renderbuffer *src_irb, mesa_format src_format,
              struct intel_renderbuffer *dst_irb, mesa_format dst_format,
              GLfloat srcX0, GLfloat srcY0, GLfloat srcX1, GLfloat srcY1,
              GLfloat dstX0, GLfloat dstY0, GLfloat dstX1, GLfloat dstY1,
              GLenum filter, bool mirror_x, bool mirror_y)
{
   /* Find source/dst miptrees */
   struct intel_mipmap_tree *src_mt = find_miptree(buffer_bit, src_irb);
   struct intel_mipmap_tree *dst_mt = find_miptree(buffer_bit, dst_irb);

   const bool es3 = _mesa_is_gles3(&brw->ctx);
   /* Do the blit */
   brw_blorp_blit_miptrees(brw,
                           src_mt, src_irb->mt_level, src_irb->mt_layer,
                           src_format, blorp_get_texture_swizzle(src_irb),
                           dst_mt, dst_irb->mt_level, dst_irb->mt_layer,
                           dst_format,
                           srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1,
                           filter, mirror_x, mirror_y,
                           es3, es3);

   dst_irb->need_downsample = true;
}

static bool
try_blorp_blit(struct brw_context *brw,
               const struct gl_framebuffer *read_fb,
               const struct gl_framebuffer *draw_fb,
               GLfloat srcX0, GLfloat srcY0, GLfloat srcX1, GLfloat srcY1,
               GLfloat dstX0, GLfloat dstY0, GLfloat dstX1, GLfloat dstY1,
               GLenum filter, GLbitfield buffer_bit)
{
   struct gl_context *ctx = &brw->ctx;

   /* Sync up the state of window system buffers.  We need to do this before
    * we go looking for the buffers.
    */
   intel_prepare_render(brw);

   bool mirror_x, mirror_y;
   if (brw_meta_mirror_clip_and_scissor(ctx, read_fb, draw_fb,
                                        &srcX0, &srcY0, &srcX1, &srcY1,
                                        &dstX0, &dstY0, &dstX1, &dstY1,
                                        &mirror_x, &mirror_y))
      return true;

   /* Find buffers */
   struct intel_renderbuffer *src_irb;
   struct intel_renderbuffer *dst_irb;
   struct intel_mipmap_tree *src_mt;
   struct intel_mipmap_tree *dst_mt;
   switch (buffer_bit) {
   case GL_COLOR_BUFFER_BIT:
      src_irb = intel_renderbuffer(read_fb->_ColorReadBuffer);
      for (unsigned i = 0; i < draw_fb->_NumColorDrawBuffers; ++i) {
         dst_irb = intel_renderbuffer(draw_fb->_ColorDrawBuffers[i]);
	 if (dst_irb)
            do_blorp_blit(brw, buffer_bit,
                          src_irb, src_irb->Base.Base.Format,
                          dst_irb, dst_irb->Base.Base.Format,
                          srcX0, srcY0, srcX1, srcY1,
                          dstX0, dstY0, dstX1, dstY1,
                          filter, mirror_x, mirror_y);
      }
      break;
   case GL_DEPTH_BUFFER_BIT:
      src_irb =
         intel_renderbuffer(read_fb->Attachment[BUFFER_DEPTH].Renderbuffer);
      dst_irb =
         intel_renderbuffer(draw_fb->Attachment[BUFFER_DEPTH].Renderbuffer);
      src_mt = find_miptree(buffer_bit, src_irb);
      dst_mt = find_miptree(buffer_bit, dst_irb);

      /* We can't handle format conversions between Z24 and other formats
       * since we have to lie about the surface format. See the comments in
       * brw_blorp_surface_info::set().
       */
      if ((src_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT) !=
          (dst_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT))
         return false;

      do_blorp_blit(brw, buffer_bit, src_irb, MESA_FORMAT_NONE,
                    dst_irb, MESA_FORMAT_NONE, srcX0, srcY0,
                    srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                    filter, mirror_x, mirror_y);
      break;
   case GL_STENCIL_BUFFER_BIT:
      src_irb =
         intel_renderbuffer(read_fb->Attachment[BUFFER_STENCIL].Renderbuffer);
      dst_irb =
         intel_renderbuffer(draw_fb->Attachment[BUFFER_STENCIL].Renderbuffer);
      do_blorp_blit(brw, buffer_bit, src_irb, MESA_FORMAT_NONE,
                    dst_irb, MESA_FORMAT_NONE, srcX0, srcY0,
                    srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                    filter, mirror_x, mirror_y);
      break;
   default:
      unreachable("not reached");
   }

   return true;
}

bool
brw_blorp_copytexsubimage(struct brw_context *brw,
                          struct gl_renderbuffer *src_rb,
                          struct gl_texture_image *dst_image,
                          int slice,
                          int srcX0, int srcY0,
                          int dstX0, int dstY0,
                          int width, int height)
{
   struct gl_context *ctx = &brw->ctx;
   struct intel_renderbuffer *src_irb = intel_renderbuffer(src_rb);
   struct intel_texture_image *intel_image = intel_texture_image(dst_image);

   /* No pixel transfer operations (zoom, bias, mapping), just a blit */
   if (brw->ctx._ImageTransferState)
      return false;

   /* Sync up the state of window system buffers.  We need to do this before
    * we go looking at the src renderbuffer's miptree.
    */
   intel_prepare_render(brw);

   struct intel_mipmap_tree *src_mt = src_irb->mt;
   struct intel_mipmap_tree *dst_mt = intel_image->mt;

   /* There is support for only up to eight samples. */
   if (src_mt->num_samples > 8 || dst_mt->num_samples > 8)
      return false;

   /* BLORP is only supported from Gen6 onwards. */
   if (brw->gen < 6)
      return false;

   if (_mesa_get_format_base_format(src_rb->Format) !=
       _mesa_get_format_base_format(dst_image->TexFormat)) {
      return false;
   }

   /* We can't handle format conversions between Z24 and other formats since
    * we have to lie about the surface format.  See the comments in
    * brw_blorp_surface_info::set().
    */
   if ((src_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT) !=
       (dst_mt->format == MESA_FORMAT_Z24_UNORM_X8_UINT)) {
      return false;
   }

   if (!brw->format_supported_as_render_target[dst_image->TexFormat])
      return false;

   /* Source clipping shouldn't be necessary, since copytexsubimage (in
    * src/mesa/main/teximage.c) calls _mesa_clip_copytexsubimage() which
    * takes care of it.
    *
    * Destination clipping shouldn't be necessary since the restrictions on
    * glCopyTexSubImage prevent the user from specifying a destination rectangle
    * that falls outside the bounds of the destination texture.
    * See error_check_subtexture_dimensions().
    */

   int srcY1 = srcY0 + height;
   int srcX1 = srcX0 + width;
   int dstX1 = dstX0 + width;
   int dstY1 = dstY0 + height;

   /* Account for the fact that in the system framebuffer, the origin is at
    * the lower left.
    */
   bool mirror_y = false;
   if (_mesa_is_winsys_fbo(ctx->ReadBuffer)) {
      GLint tmp = src_rb->Height - srcY0;
      srcY0 = src_rb->Height - srcY1;
      srcY1 = tmp;
      mirror_y = true;
   }

   /* Account for face selection and texture view MinLayer */
   int dst_slice = slice + dst_image->TexObject->MinLayer + dst_image->Face;
   int dst_level = dst_image->Level + dst_image->TexObject->MinLevel;

   brw_blorp_blit_miptrees(brw,
                           src_mt, src_irb->mt_level, src_irb->mt_layer,
                           src_rb->Format, blorp_get_texture_swizzle(src_irb),
                           dst_mt, dst_level, dst_slice,
                           dst_image->TexFormat,
                           srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1,
                           GL_NEAREST, false, mirror_y,
                           false, false);

   /* If we're copying to a packed depth stencil texture and the source
    * framebuffer has separate stencil, we need to also copy the stencil data
    * over.
    */
   src_rb = ctx->ReadBuffer->Attachment[BUFFER_STENCIL].Renderbuffer;
   if (_mesa_get_format_bits(dst_image->TexFormat, GL_STENCIL_BITS) > 0 &&
       src_rb != NULL) {
      src_irb = intel_renderbuffer(src_rb);
      src_mt = src_irb->mt;

      if (src_mt->stencil_mt)
         src_mt = src_mt->stencil_mt;
      if (dst_mt->stencil_mt)
         dst_mt = dst_mt->stencil_mt;

      if (src_mt != dst_mt) {
         brw_blorp_blit_miptrees(brw,
                                 src_mt, src_irb->mt_level, src_irb->mt_layer,
                                 src_mt->format,
                                 blorp_get_texture_swizzle(src_irb),
                                 dst_mt, dst_level, dst_slice,
                                 dst_mt->format,
                                 srcX0, srcY0, srcX1, srcY1,
                                 dstX0, dstY0, dstX1, dstY1,
                                 GL_NEAREST, false, mirror_y,
                                 false, false);
      }
   }

   return true;
}


GLbitfield
brw_blorp_framebuffer(struct brw_context *brw,
                      struct gl_framebuffer *readFb,
                      struct gl_framebuffer *drawFb,
                      GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                      GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                      GLbitfield mask, GLenum filter)
{
   /* BLORP is not supported before Gen6. */
   if (brw->gen < 6)
      return mask;

   static GLbitfield buffer_bits[] = {
      GL_COLOR_BUFFER_BIT,
      GL_DEPTH_BUFFER_BIT,
      GL_STENCIL_BUFFER_BIT,
   };

   for (unsigned int i = 0; i < ARRAY_SIZE(buffer_bits); ++i) {
      if ((mask & buffer_bits[i]) &&
       try_blorp_blit(brw, readFb, drawFb,
                      srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      filter, buffer_bits[i])) {
         mask &= ~buffer_bits[i];
      }
   }

   return mask;
}


/**
 * Enum to specify the order of arguments in a sampler message
 */
enum sampler_message_arg
{
   SAMPLER_MESSAGE_ARG_U_FLOAT,
   SAMPLER_MESSAGE_ARG_V_FLOAT,
   SAMPLER_MESSAGE_ARG_U_INT,
   SAMPLER_MESSAGE_ARG_V_INT,
   SAMPLER_MESSAGE_ARG_R_INT,
   SAMPLER_MESSAGE_ARG_SI_INT,
   SAMPLER_MESSAGE_ARG_MCS_INT,
   SAMPLER_MESSAGE_ARG_ZERO_INT,
};

struct brw_blorp_blit_vars {
   /* Uniforms values from brw_blorp_wm_push_constants */
   nir_variable *u_dst_x0;
   nir_variable *u_dst_x1;
   nir_variable *u_dst_y0;
   nir_variable *u_dst_y1;
   nir_variable *u_rect_grid_x1;
   nir_variable *u_rect_grid_y1;
   struct {
      nir_variable *multiplier;
      nir_variable *offset;
   } u_x_transform, u_y_transform;
   nir_variable *u_src_z;

   /* gl_FragCoord */
   nir_variable *frag_coord;

   /* gl_FragColor */
   nir_variable *color_out;
};

static void
brw_blorp_blit_vars_init(nir_builder *b, struct brw_blorp_blit_vars *v,
                         const struct brw_blorp_blit_prog_key *key)
{
#define LOAD_UNIFORM(name, type)\
   v->u_##name = nir_variable_create(b->shader, nir_var_uniform, type, #name); \
   v->u_##name->data.location = \
      offsetof(struct brw_blorp_wm_push_constants, name);

   LOAD_UNIFORM(dst_x0, glsl_uint_type())
   LOAD_UNIFORM(dst_x1, glsl_uint_type())
   LOAD_UNIFORM(dst_y0, glsl_uint_type())
   LOAD_UNIFORM(dst_y1, glsl_uint_type())
   LOAD_UNIFORM(rect_grid_x1, glsl_float_type())
   LOAD_UNIFORM(rect_grid_y1, glsl_float_type())
   LOAD_UNIFORM(x_transform.multiplier, glsl_float_type())
   LOAD_UNIFORM(x_transform.offset, glsl_float_type())
   LOAD_UNIFORM(y_transform.multiplier, glsl_float_type())
   LOAD_UNIFORM(y_transform.offset, glsl_float_type())
   LOAD_UNIFORM(src_z, glsl_uint_type())

#undef DECL_UNIFORM

   v->frag_coord = nir_variable_create(b->shader, nir_var_shader_in,
                                       glsl_vec4_type(), "gl_FragCoord");
   v->frag_coord->data.location = VARYING_SLOT_POS;
   v->frag_coord->data.origin_upper_left = true;

   v->color_out = nir_variable_create(b->shader, nir_var_shader_out,
                                      glsl_vec4_type(), "gl_FragColor");
   v->color_out->data.location = FRAG_RESULT_COLOR;
}

nir_ssa_def *
blorp_blit_get_frag_coords(nir_builder *b,
                           const struct brw_blorp_blit_prog_key *key,
                           struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *coord = nir_f2i(b, nir_load_var(b, v->frag_coord));

   if (key->persample_msaa_dispatch) {
      return nir_vec3(b, nir_channel(b, coord, 0), nir_channel(b, coord, 1),
         nir_load_system_value(b, nir_intrinsic_load_sample_id, 0));
   } else {
      return nir_vec2(b, nir_channel(b, coord, 0), nir_channel(b, coord, 1));
   }
}

/**
 * Emit code to translate from destination (X, Y) coordinates to source (X, Y)
 * coordinates.
 */
nir_ssa_def *
blorp_blit_apply_transform(nir_builder *b, nir_ssa_def *src_pos,
                           struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *offset = nir_vec2(b, nir_load_var(b, v->u_x_transform.offset),
                                     nir_load_var(b, v->u_y_transform.offset));
   nir_ssa_def *mul = nir_vec2(b, nir_load_var(b, v->u_x_transform.multiplier),
                                  nir_load_var(b, v->u_y_transform.multiplier));

   return nir_ffma(b, src_pos, mul, offset);
}

static inline void
blorp_nir_discard_if_outside_rect(nir_builder *b, nir_ssa_def *pos,
                                  struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *c0, *c1, *c2, *c3;
   c0 = nir_ult(b, nir_channel(b, pos, 0), nir_load_var(b, v->u_dst_x0));
   c1 = nir_uge(b, nir_channel(b, pos, 0), nir_load_var(b, v->u_dst_x1));
   c2 = nir_ult(b, nir_channel(b, pos, 1), nir_load_var(b, v->u_dst_y0));
   c3 = nir_uge(b, nir_channel(b, pos, 1), nir_load_var(b, v->u_dst_y1));
   nir_ssa_def *oob = nir_ior(b, nir_ior(b, c0, c1), nir_ior(b, c2, c3));

   nir_intrinsic_instr *discard =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard_if);
   discard->src[0] = nir_src_for_ssa(oob);
   nir_builder_instr_insert(b, &discard->instr);
}

static nir_tex_instr *
blorp_create_nir_tex_instr(nir_shader *shader, nir_texop op,
                           nir_ssa_def *pos, unsigned num_srcs,
                           enum brw_reg_type dst_type)
{
   nir_tex_instr *tex = nir_tex_instr_create(shader, num_srcs);

   tex->op = op;

   switch (dst_type) {
   case BRW_REGISTER_TYPE_F:
      tex->dest_type = nir_type_float;
      break;
   case BRW_REGISTER_TYPE_D:
      tex->dest_type = nir_type_int;
      break;
   case BRW_REGISTER_TYPE_UD:
      tex->dest_type = nir_type_uint;
      break;
   default:
      unreachable("Invalid texture return type");
   }

   tex->is_array = false;
   tex->is_shadow = false;

   /* Blorp only has one texture and it's bound at unit 0 */
   tex->texture = NULL;
   tex->sampler = NULL;
   tex->texture_index = 0;
   tex->sampler_index = 0;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);

   return tex;
}

static nir_ssa_def *
blorp_nir_tex(nir_builder *b, nir_ssa_def *pos, enum brw_reg_type dst_type)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b->shader, nir_texop_tex, pos, 2, dst_type);

   assert(pos->num_components == 2);
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->coord_components = 2;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(pos);
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));

   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
blorp_nir_txf(nir_builder *b, struct brw_blorp_blit_vars *v,
              nir_ssa_def *pos, enum brw_reg_type dst_type)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b->shader, nir_texop_txf, pos, 2, dst_type);

   /* In order to properly handle 3-D textures, we pull the Z component from
    * a uniform.  TODO: This is a bit magic; we should probably make this
    * more explicit in the future.
    */
   assert(pos->num_components == 2);
   pos = nir_vec3(b, nir_channel(b, pos, 0), nir_channel(b, pos, 1),
                     nir_load_var(b, v->u_src_z));

   tex->sampler_dim = GLSL_SAMPLER_DIM_3D;
   tex->coord_components = 3;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(pos);
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));

   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
blorp_nir_txf_ms(nir_builder *b, nir_ssa_def *pos, nir_ssa_def *mcs,
                 enum brw_reg_type dst_type)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b->shader, nir_texop_txf_ms, pos,
                                 mcs != NULL ? 3 : 2, dst_type);

   tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
   tex->coord_components = 2;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(pos);

   tex->src[1].src_type = nir_tex_src_ms_index;
   if (pos->num_components == 2) {
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));
   } else {
      assert(pos->num_components == 3);
      tex->src[1].src = nir_src_for_ssa(nir_channel(b, pos, 2));
   }

   if (mcs) {
      tex->src[2].src_type = nir_tex_src_ms_mcs;
      tex->src[2].src = nir_src_for_ssa(mcs);
   }

   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
blorp_nir_txf_ms_mcs(nir_builder *b, nir_ssa_def *pos)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b->shader, nir_texop_txf_ms_mcs,
                                 pos, 1, BRW_REGISTER_TYPE_D);

   tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
   tex->coord_components = 2;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(pos);

   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
nir_mask_shift_or(struct nir_builder *b, nir_ssa_def *dst, nir_ssa_def *src,
                  uint32_t src_mask, int src_left_shift)
{
   nir_ssa_def *masked = nir_iand(b, src, nir_imm_int(b, src_mask));

   nir_ssa_def *shifted;
   if (src_left_shift > 0) {
      shifted = nir_ishl(b, masked, nir_imm_int(b, src_left_shift));
   } else if (src_left_shift < 0) {
      shifted = nir_ushr(b, masked, nir_imm_int(b, -src_left_shift));
   } else {
      assert(src_left_shift == 0);
      shifted = masked;
   }

   return nir_ior(b, dst, shifted);
}

/**
 * Emit code to compensate for the difference between Y and W tiling.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y', S') = detile(W-MAJOR, tile(Y-MAJOR, X, Y, S))
 *
 * (See brw_blorp_build_nir_shader).
 */
static inline nir_ssa_def *
blorp_nir_retile_y_to_w(nir_builder *b, nir_ssa_def *pos)
{
   assert(pos->num_components == 2);
   nir_ssa_def *x_Y = nir_channel(b, pos, 0);
   nir_ssa_def *y_Y = nir_channel(b, pos, 1);

   /* Given X and Y coordinates that describe an address using Y tiling,
    * translate to the X and Y coordinates that describe the same address
    * using W tiling.
    *
    * If we break down the low order bits of X and Y, using a
    * single letter to represent each low-order bit:
    *
    *   X = A << 7 | 0bBCDEFGH
    *   Y = J << 5 | 0bKLMNP                                       (1)
    *
    * Then we can apply the Y tiling formula to see the memory offset being
    * addressed:
    *
    *   offset = (J * tile_pitch + A) << 12 | 0bBCDKLMNPEFGH       (2)
    *
    * If we apply the W detiling formula to this memory location, that the
    * corresponding X' and Y' coordinates are:
    *
    *   X' = A << 6 | 0bBCDPFH                                     (3)
    *   Y' = J << 6 | 0bKLMNEG
    *
    * Combining (1) and (3), we see that to transform (X, Y) to (X', Y'),
    * we need to make the following computation:
    *
    *   X' = (X & ~0b1011) >> 1 | (Y & 0b1) << 2 | X & 0b1         (4)
    *   Y' = (Y & ~0b1) << 1 | (X & 0b1000) >> 2 | (X & 0b10) >> 1
    */
   nir_ssa_def *x_W = nir_imm_int(b, 0);
   x_W = nir_mask_shift_or(b, x_W, x_Y, 0xfffffff4, -1);
   x_W = nir_mask_shift_or(b, x_W, y_Y, 0x1, 2);
   x_W = nir_mask_shift_or(b, x_W, x_Y, 0x1, 0);

   nir_ssa_def *y_W = nir_imm_int(b, 0);
   y_W = nir_mask_shift_or(b, y_W, y_Y, 0xfffffffe, 1);
   y_W = nir_mask_shift_or(b, y_W, x_Y, 0x8, -2);
   y_W = nir_mask_shift_or(b, y_W, x_Y, 0x2, -1);

   return nir_vec2(b, x_W, y_W);
}

/**
 * Emit code to compensate for the difference between Y and W tiling.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y', S') = detile(Y-MAJOR, tile(W-MAJOR, X, Y, S))
 *
 * (See brw_blorp_build_nir_shader).
 */
static inline nir_ssa_def *
blorp_nir_retile_w_to_y(nir_builder *b, nir_ssa_def *pos)
{
   assert(pos->num_components == 2);
   nir_ssa_def *x_W = nir_channel(b, pos, 0);
   nir_ssa_def *y_W = nir_channel(b, pos, 1);

   /* Applying the same logic as above, but in reverse, we obtain the
    * formulas:
    *
    * X' = (X & ~0b101) << 1 | (Y & 0b10) << 2 | (Y & 0b1) << 1 | X & 0b1
    * Y' = (Y & ~0b11) >> 1 | (X & 0b100) >> 2
    */
   nir_ssa_def *x_Y = nir_imm_int(b, 0);
   x_Y = nir_mask_shift_or(b, x_Y, x_W, 0xfffffffa, 1);
   x_Y = nir_mask_shift_or(b, x_Y, y_W, 0x2, 2);
   x_Y = nir_mask_shift_or(b, x_Y, y_W, 0x1, 1);
   x_Y = nir_mask_shift_or(b, x_Y, x_W, 0x1, 0);

   nir_ssa_def *y_Y = nir_imm_int(b, 0);
   y_Y = nir_mask_shift_or(b, y_Y, y_W, 0xfffffffc, -1);
   y_Y = nir_mask_shift_or(b, y_Y, x_W, 0x4, -2);

   return nir_vec2(b, x_Y, y_Y);
}

/**
 * Emit code to compensate for the difference between MSAA and non-MSAA
 * surfaces.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y', S') = encode_msaa(num_samples, IMS, X, Y, S)
 *
 * (See brw_blorp_blit_program).
 */
static inline nir_ssa_def *
blorp_nir_encode_msaa(nir_builder *b, nir_ssa_def *pos,
                      unsigned num_samples, enum intel_msaa_layout layout)
{
   assert(pos->num_components == 2 || pos->num_components == 3);

   switch (layout) {
   case INTEL_MSAA_LAYOUT_NONE:
      assert(pos->num_components == 2);
      return pos;
   case INTEL_MSAA_LAYOUT_CMS:
      /* We can't compensate for compressed layout since at this point in the
       * program we haven't read from the MCS buffer.
       */
      unreachable("Bad layout in encode_msaa");
   case INTEL_MSAA_LAYOUT_UMS:
      /* No translation needed */
      return pos;
   case INTEL_MSAA_LAYOUT_IMS: {
      nir_ssa_def *x_in = nir_channel(b, pos, 0);
      nir_ssa_def *y_in = nir_channel(b, pos, 1);
      nir_ssa_def *s_in = pos->num_components == 2 ? nir_imm_int(b, 0) :
                                                     nir_channel(b, pos, 2);

      nir_ssa_def *x_out = nir_imm_int(b, 0);
      nir_ssa_def *y_out = nir_imm_int(b, 0);
      switch (num_samples) {
      case 2:
      case 4:
         /* encode_msaa(2, IMS, X, Y, S) = (X', Y', 0)
          *   where X' = (X & ~0b1) << 1 | (S & 0b1) << 1 | (X & 0b1)
          *         Y' = Y
          *
          * encode_msaa(4, IMS, X, Y, S) = (X', Y', 0)
          *   where X' = (X & ~0b1) << 1 | (S & 0b1) << 1 | (X & 0b1)
          *         Y' = (Y & ~0b1) << 1 | (S & 0b10) | (Y & 0b1)
          */
         x_out = nir_mask_shift_or(b, x_out, x_in, 0xfffffffe, 1);
         x_out = nir_mask_shift_or(b, x_out, s_in, 0x1, 1);
         x_out = nir_mask_shift_or(b, x_out, x_in, 0x1, 0);
         if (num_samples == 2) {
            y_out = y_in;
         } else {
            y_out = nir_mask_shift_or(b, y_out, y_in, 0xfffffffe, 1);
            y_out = nir_mask_shift_or(b, y_out, s_in, 0x2, 0);
            y_out = nir_mask_shift_or(b, y_out, y_in, 0x1, 0);
         }
         break;

      case 8:
         /* encode_msaa(8, IMS, X, Y, S) = (X', Y', 0)
          *   where X' = (X & ~0b1) << 2 | (S & 0b100) | (S & 0b1) << 1
          *              | (X & 0b1)
          *         Y' = (Y & ~0b1) << 1 | (S & 0b10) | (Y & 0b1)
          */
         x_out = nir_mask_shift_or(b, x_out, x_in, 0xfffffffe, 2);
         x_out = nir_mask_shift_or(b, x_out, s_in, 0x4, 0);
         x_out = nir_mask_shift_or(b, x_out, s_in, 0x1, 1);
         x_out = nir_mask_shift_or(b, x_out, x_in, 0x1, 0);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0xfffffffe, 1);
         y_out = nir_mask_shift_or(b, y_out, s_in, 0x2, 0);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0x1, 0);
         break;

      case 16:
         /* encode_msaa(16, IMS, X, Y, S) = (X', Y', 0)
          *   where X' = (X & ~0b1) << 2 | (S & 0b100) | (S & 0b1) << 1
          *              | (X & 0b1)
          *         Y' = (Y & ~0b1) << 2 | (S & 0b1000) >> 1 (S & 0b10)
          *              | (Y & 0b1)
          */
         x_out = nir_mask_shift_or(b, x_out, x_in, 0xfffffffe, 2);
         x_out = nir_mask_shift_or(b, x_out, s_in, 0x4, 0);
         x_out = nir_mask_shift_or(b, x_out, s_in, 0x1, 1);
         x_out = nir_mask_shift_or(b, x_out, x_in, 0x1, 0);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0xfffffffe, 2);
         y_out = nir_mask_shift_or(b, y_out, s_in, 0x8, -1);
         y_out = nir_mask_shift_or(b, y_out, s_in, 0x2, 0);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0x1, 0);
         break;

      default:
         unreachable("Invalid number of samples for IMS layout");
      }

      return nir_vec2(b, x_out, y_out);
   }

   default:
      unreachable("Invalid MSAA layout");
   }
}

/**
 * Emit code to compensate for the difference between MSAA and non-MSAA
 * surfaces.
 *
 * This code modifies the X and Y coordinates according to the formula:
 *
 *   (X', Y', S) = decode_msaa(num_samples, IMS, X, Y, S)
 *
 * (See brw_blorp_blit_program).
 */
static inline nir_ssa_def *
blorp_nir_decode_msaa(nir_builder *b, nir_ssa_def *pos,
                      unsigned num_samples, enum intel_msaa_layout layout)
{
   assert(pos->num_components == 2 || pos->num_components == 3);

   switch (layout) {
   case INTEL_MSAA_LAYOUT_NONE:
      /* No translation necessary, and S should already be zero. */
      assert(pos->num_components == 2);
      return pos;
   case INTEL_MSAA_LAYOUT_CMS:
      /* We can't compensate for compressed layout since at this point in the
       * program we don't have access to the MCS buffer.
       */
      unreachable("Bad layout in encode_msaa");
   case INTEL_MSAA_LAYOUT_UMS:
      /* No translation necessary. */
      return pos;
   case INTEL_MSAA_LAYOUT_IMS: {
      assert(pos->num_components == 2);

      nir_ssa_def *x_in = nir_channel(b, pos, 0);
      nir_ssa_def *y_in = nir_channel(b, pos, 1);

      nir_ssa_def *x_out = nir_imm_int(b, 0);
      nir_ssa_def *y_out = nir_imm_int(b, 0);
      nir_ssa_def *s_out = nir_imm_int(b, 0);
      switch (num_samples) {
      case 2:
      case 4:
         /* decode_msaa(2, IMS, X, Y, 0) = (X', Y', S)
          *   where X' = (X & ~0b11) >> 1 | (X & 0b1)
          *         S = (X & 0b10) >> 1
          *
          * decode_msaa(4, IMS, X, Y, 0) = (X', Y', S)
          *   where X' = (X & ~0b11) >> 1 | (X & 0b1)
          *         Y' = (Y & ~0b11) >> 1 | (Y & 0b1)
          *         S = (Y & 0b10) | (X & 0b10) >> 1
          */
         x_out = nir_mask_shift_or(b, x_out, x_in, 0xfffffffc, -1);
         x_out = nir_mask_shift_or(b, x_out, x_in, 0x1, 0);
         if (num_samples == 2) {
            y_out = y_in;
            s_out = nir_mask_shift_or(b, s_out, x_in, 0x2, -1);
         } else {
            y_out = nir_mask_shift_or(b, y_out, y_in, 0xfffffffc, -1);
            y_out = nir_mask_shift_or(b, y_out, y_in, 0x1, 0);
            s_out = nir_mask_shift_or(b, s_out, x_in, 0x2, -1);
            s_out = nir_mask_shift_or(b, s_out, y_in, 0x2, 0);
         }
         break;

      case 8:
         /* decode_msaa(8, IMS, X, Y, 0) = (X', Y', S)
          *   where X' = (X & ~0b111) >> 2 | (X & 0b1)
          *         Y' = (Y & ~0b11) >> 1 | (Y & 0b1)
          *         S = (X & 0b100) | (Y & 0b10) | (X & 0b10) >> 1
          */
         x_out = nir_mask_shift_or(b, x_out, x_in, 0xfffffff8, -2);
         x_out = nir_mask_shift_or(b, x_out, x_in, 0x1, 0);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0xfffffffc, -1);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0x1, 0);
         s_out = nir_mask_shift_or(b, s_out, x_in, 0x4, 0);
         s_out = nir_mask_shift_or(b, s_out, y_in, 0x2, 0);
         s_out = nir_mask_shift_or(b, s_out, x_in, 0x2, -1);
         break;

      case 16:
         /* decode_msaa(16, IMS, X, Y, 0) = (X', Y', S)
          *   where X' = (X & ~0b111) >> 2 | (X & 0b1)
          *         Y' = (Y & ~0b111) >> 2 | (Y & 0b1)
          *         S = (Y & 0b100) << 1 | (X & 0b100) |
          *             (Y & 0b10) | (X & 0b10) >> 1
          */
         x_out = nir_mask_shift_or(b, x_out, x_in, 0xfffffff8, -2);
         x_out = nir_mask_shift_or(b, x_out, x_in, 0x1, 0);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0xfffffff8, -2);
         y_out = nir_mask_shift_or(b, y_out, y_in, 0x1, 0);
         s_out = nir_mask_shift_or(b, s_out, y_in, 0x4, 1);
         s_out = nir_mask_shift_or(b, s_out, x_in, 0x4, 0);
         s_out = nir_mask_shift_or(b, s_out, y_in, 0x2, 0);
         s_out = nir_mask_shift_or(b, s_out, x_in, 0x2, -1);
         break;

      default:
         unreachable("Invalid number of samples for IMS layout");
      }

      return nir_vec3(b, x_out, y_out, s_out);
   }

   default:
      unreachable("Invalid MSAA layout");
   }
}

/**
 * Count the number of trailing 1 bits in the given value.  For example:
 *
 * count_trailing_one_bits(0) == 0
 * count_trailing_one_bits(7) == 3
 * count_trailing_one_bits(11) == 2
 */
static inline int count_trailing_one_bits(unsigned value)
{
#ifdef HAVE___BUILTIN_CTZ
   return __builtin_ctz(~value);
#else
   return _mesa_bitcount(value & ~(value + 1));
#endif
}

static nir_ssa_def *
blorp_nir_manual_blend_average(nir_builder *b, nir_ssa_def *pos,
                               unsigned tex_samples,
                               enum intel_msaa_layout tex_layout,
                               enum brw_reg_type dst_type)
{
   /* If non-null, this is the outer-most if statement */
   nir_if *outer_if = NULL;

   nir_variable *color =
      nir_local_variable_create(b->impl, glsl_vec4_type(), "color");

   nir_ssa_def *mcs = NULL;
   if (tex_layout == INTEL_MSAA_LAYOUT_CMS)
      mcs = blorp_nir_txf_ms_mcs(b, pos);

   /* We add together samples using a binary tree structure, e.g. for 4x MSAA:
    *
    *   result = ((sample[0] + sample[1]) + (sample[2] + sample[3])) / 4
    *
    * This ensures that when all samples have the same value, no numerical
    * precision is lost, since each addition operation always adds two equal
    * values, and summing two equal floating point values does not lose
    * precision.
    *
    * We perform this computation by treating the texture_data array as a
    * stack and performing the following operations:
    *
    * - push sample 0 onto stack
    * - push sample 1 onto stack
    * - add top two stack entries
    * - push sample 2 onto stack
    * - push sample 3 onto stack
    * - add top two stack entries
    * - add top two stack entries
    * - divide top stack entry by 4
    *
    * Note that after pushing sample i onto the stack, the number of add
    * operations we do is equal to the number of trailing 1 bits in i.  This
    * works provided the total number of samples is a power of two, which it
    * always is for i965.
    *
    * For integer formats, we replace the add operations with average
    * operations and skip the final division.
    */
   nir_ssa_def *texture_data[5];
   unsigned stack_depth = 0;
   for (unsigned i = 0; i < tex_samples; ++i) {
      assert(stack_depth == _mesa_bitcount(i)); /* Loop invariant */

      /* Push sample i onto the stack */
      assert(stack_depth < ARRAY_SIZE(texture_data));

      nir_ssa_def *ms_pos = nir_vec3(b, nir_channel(b, pos, 0),
                                        nir_channel(b, pos, 1),
                                        nir_imm_int(b, i));
      texture_data[stack_depth++] = blorp_nir_txf_ms(b, ms_pos, mcs, dst_type);

      if (i == 0 && tex_layout == INTEL_MSAA_LAYOUT_CMS) {
         /* The Ivy Bridge PRM, Vol4 Part1 p27 (Multisample Control Surface)
          * suggests an optimization:
          *
          *     "A simple optimization with probable large return in
          *     performance is to compare the MCS value to zero (indicating
          *     all samples are on sample slice 0), and sample only from
          *     sample slice 0 using ld2dss if MCS is zero."
          *
          * Note that in the case where the MCS value is zero, sampling from
          * sample slice 0 using ld2dss and sampling from sample 0 using
          * ld2dms are equivalent (since all samples are on sample slice 0).
          * Since we have already sampled from sample 0, all we need to do is
          * skip the remaining fetches and averaging if MCS is zero.
          */
         nir_ssa_def *mcs_zero =
            nir_ieq(b, nir_channel(b, mcs, 0), nir_imm_int(b, 0));
         if (tex_samples == 16) {
            mcs_zero = nir_iand(b, mcs_zero,
               nir_ieq(b, nir_channel(b, mcs, 1), nir_imm_int(b, 0)));
         }

         nir_if *if_stmt = nir_if_create(b->shader);
         if_stmt->condition = nir_src_for_ssa(mcs_zero);
         nir_cf_node_insert(b->cursor, &if_stmt->cf_node);

         b->cursor = nir_after_cf_list(&if_stmt->then_list);
         nir_store_var(b, color, texture_data[0], 0xf);

         b->cursor = nir_after_cf_list(&if_stmt->else_list);
         outer_if = if_stmt;
      }

      for (int j = 0; j < count_trailing_one_bits(i); j++) {
         assert(stack_depth >= 2);
         --stack_depth;

         assert(dst_type == BRW_REGISTER_TYPE_F);
         texture_data[stack_depth - 1] =
            nir_fadd(b, texture_data[stack_depth - 1],
                        texture_data[stack_depth]);
      }
   }

   /* We should have just 1 sample on the stack now. */
   assert(stack_depth == 1);

   texture_data[0] = nir_fmul(b, texture_data[0],
                              nir_imm_float(b, 1.0 / tex_samples));

   nir_store_var(b, color, texture_data[0], 0xf);

   if (outer_if)
      b->cursor = nir_after_cf_node(&outer_if->cf_node);

   return nir_load_var(b, color);
}

static inline nir_ssa_def *
nir_imm_vec2(nir_builder *build, float x, float y)
{
   nir_const_value v;

   memset(&v, 0, sizeof(v));
   v.f32[0] = x;
   v.f32[1] = y;

   return nir_build_imm(build, 4, 32, v);
}

static nir_ssa_def *
blorp_nir_manual_blend_bilinear(nir_builder *b, nir_ssa_def *pos,
                                unsigned tex_samples,
                                const brw_blorp_blit_prog_key *key,
                                struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *pos_xy = nir_channels(b, pos, 0x3);

   nir_ssa_def *scale = nir_imm_vec2(b, key->x_scale, key->y_scale);

   /* Translate coordinates to lay out the samples in a rectangular  grid
    * roughly corresponding to sample locations.
    */
   pos_xy = nir_fmul(b, pos_xy, scale);
   /* Adjust coordinates so that integers represent pixel centers rather
    * than pixel edges.
    */
   pos_xy = nir_fadd(b, pos_xy, nir_imm_float(b, -0.5));
   /* Clamp the X, Y texture coordinates to properly handle the sampling of
    * texels on texture edges.
    */
   pos_xy = nir_fmin(b, nir_fmax(b, pos_xy, nir_imm_float(b, 0.0)),
                        nir_vec2(b, nir_load_var(b, v->u_rect_grid_x1),
                                    nir_load_var(b, v->u_rect_grid_y1)));

   /* Store the fractional parts to be used as bilinear interpolation
    * coefficients.
    */
   nir_ssa_def *frac_xy = nir_ffract(b, pos_xy);
   /* Round the float coordinates down to nearest integer */
   pos_xy = nir_fdiv(b, nir_ftrunc(b, pos_xy), scale);

   nir_ssa_def *tex_data[4];
   for (unsigned i = 0; i < 4; ++i) {
      float sample_off_x = (float)(i & 0x1) / key->x_scale;
      float sample_off_y = (float)((i >> 1) & 0x1) / key->y_scale;
      nir_ssa_def *sample_off = nir_imm_vec2(b, sample_off_x, sample_off_y);

      nir_ssa_def *sample_coords = nir_fadd(b, pos_xy, sample_off);
      nir_ssa_def *sample_coords_int = nir_f2i(b, sample_coords);

      /* The MCS value we fetch has to match up with the pixel that we're
       * sampling from. Since we sample from different pixels in each
       * iteration of this "for" loop, the call to mcs_fetch() should be
       * here inside the loop after computing the pixel coordinates.
       */
      nir_ssa_def *mcs = NULL;
      if (key->tex_layout == INTEL_MSAA_LAYOUT_CMS)
         mcs = blorp_nir_txf_ms_mcs(b, sample_coords_int);

      /* Compute sample index and map the sample index to a sample number.
       * Sample index layout shows the numbering of slots in a rectangular
       * grid of samples with in a pixel. Sample number layout shows the
       * rectangular grid of samples roughly corresponding to the real sample
       * locations with in a pixel.
       * In case of 4x MSAA, layout of sample indices matches the layout of
       * sample numbers:
       *           ---------
       *           | 0 | 1 |
       *           ---------
       *           | 2 | 3 |
       *           ---------
       *
       * In case of 8x MSAA the two layouts don't match.
       * sample index layout :  ---------    sample number layout :  ---------
       *                        | 0 | 1 |                            | 5 | 2 |
       *                        ---------                            ---------
       *                        | 2 | 3 |                            | 4 | 6 |
       *                        ---------                            ---------
       *                        | 4 | 5 |                            | 0 | 3 |
       *                        ---------                            ---------
       *                        | 6 | 7 |                            | 7 | 1 |
       *                        ---------                            ---------
       *
       * Fortunately, this can be done fairly easily as:
       * S' = (0x17306425 >> (S * 4)) & 0xf
       *
       * In the case of 16x MSAA the two layouts don't match.
       * Sample index layout:                Sample number layout:
       * ---------------------               ---------------------
       * |  0 |  1 |  2 |  3 |               | 15 | 10 |  9 |  7 |
       * ---------------------               ---------------------
       * |  4 |  5 |  6 |  7 |               |  4 |  1 |  3 | 13 |
       * ---------------------               ---------------------
       * |  8 |  9 | 10 | 11 |               | 12 |  2 |  0 |  6 |
       * ---------------------               ---------------------
       * | 12 | 13 | 14 | 15 |               | 11 |  8 |  5 | 14 |
       * ---------------------               ---------------------
       *
       * This is equivalent to
       * S' = (0xe58b602cd31479af >> (S * 4)) & 0xf
       */
      nir_ssa_def *frac = nir_ffract(b, sample_coords);
      nir_ssa_def *sample =
         nir_fdot2(b, frac, nir_imm_vec2(b, key->x_scale,
                                            key->x_scale * key->y_scale));
      sample = nir_f2i(b, sample);

      if (tex_samples == 8) {
         sample = nir_iand(b, nir_ishr(b, nir_imm_int(b, 0x17306425),
                                       nir_ishl(b, sample, nir_imm_int(b, 2))),
                           nir_imm_int(b, 0xf));
      } else if (tex_samples == 16) {
         nir_ssa_def *sample_low =
            nir_iand(b, nir_ishr(b, nir_imm_int(b, 0xd31479af),
                                 nir_ishl(b, sample, nir_imm_int(b, 2))),
                     nir_imm_int(b, 0xf));
         nir_ssa_def *sample_high =
            nir_iand(b, nir_ishr(b, nir_imm_int(b, 0xe58b602c),
                                 nir_ishl(b, nir_iadd(b, sample,
                                                      nir_imm_int(b, -8)),
                                          nir_imm_int(b, 2))),
                     nir_imm_int(b, 0xf));

         sample = nir_bcsel(b, nir_ilt(b, sample, nir_imm_int(b, 8)),
                            sample_low, sample_high);
      }
      nir_ssa_def *pos_ms = nir_vec3(b, nir_channel(b, sample_coords_int, 0),
                                        nir_channel(b, sample_coords_int, 1),
                                        sample);
      tex_data[i] = blorp_nir_txf_ms(b, pos_ms, mcs, key->texture_data_type);
   }

   nir_ssa_def *frac_x = nir_channel(b, frac_xy, 0);
   nir_ssa_def *frac_y = nir_channel(b, frac_xy, 1);
   return nir_flrp(b, nir_flrp(b, tex_data[0], tex_data[1], frac_x),
                      nir_flrp(b, tex_data[2], tex_data[3], frac_x),
                      frac_y);
}

/**
 * Generator for WM programs used in BLORP blits.
 *
 * The bulk of the work done by the WM program is to wrap and unwrap the
 * coordinate transformations used by the hardware to store surfaces in
 * memory.  The hardware transforms a pixel location (X, Y, S) (where S is the
 * sample index for a multisampled surface) to a memory offset by the
 * following formulas:
 *
 *   offset = tile(tiling_format, encode_msaa(num_samples, layout, X, Y, S))
 *   (X, Y, S) = decode_msaa(num_samples, layout, detile(tiling_format, offset))
 *
 * For a single-sampled surface, or for a multisampled surface using
 * INTEL_MSAA_LAYOUT_UMS, encode_msaa() and decode_msaa are the identity
 * function:
 *
 *   encode_msaa(1, NONE, X, Y, 0) = (X, Y, 0)
 *   decode_msaa(1, NONE, X, Y, 0) = (X, Y, 0)
 *   encode_msaa(n, UMS, X, Y, S) = (X, Y, S)
 *   decode_msaa(n, UMS, X, Y, S) = (X, Y, S)
 *
 * For a 4x multisampled surface using INTEL_MSAA_LAYOUT_IMS, encode_msaa()
 * embeds the sample number into bit 1 of the X and Y coordinates:
 *
 *   encode_msaa(4, IMS, X, Y, S) = (X', Y', 0)
 *     where X' = (X & ~0b1) << 1 | (S & 0b1) << 1 | (X & 0b1)
 *           Y' = (Y & ~0b1 ) << 1 | (S & 0b10) | (Y & 0b1)
 *   decode_msaa(4, IMS, X, Y, 0) = (X', Y', S)
 *     where X' = (X & ~0b11) >> 1 | (X & 0b1)
 *           Y' = (Y & ~0b11) >> 1 | (Y & 0b1)
 *           S = (Y & 0b10) | (X & 0b10) >> 1
 *
 * For an 8x multisampled surface using INTEL_MSAA_LAYOUT_IMS, encode_msaa()
 * embeds the sample number into bits 1 and 2 of the X coordinate and bit 1 of
 * the Y coordinate:
 *
 *   encode_msaa(8, IMS, X, Y, S) = (X', Y', 0)
 *     where X' = (X & ~0b1) << 2 | (S & 0b100) | (S & 0b1) << 1 | (X & 0b1)
 *           Y' = (Y & ~0b1) << 1 | (S & 0b10) | (Y & 0b1)
 *   decode_msaa(8, IMS, X, Y, 0) = (X', Y', S)
 *     where X' = (X & ~0b111) >> 2 | (X & 0b1)
 *           Y' = (Y & ~0b11) >> 1 | (Y & 0b1)
 *           S = (X & 0b100) | (Y & 0b10) | (X & 0b10) >> 1
 *
 * For X tiling, tile() combines together the low-order bits of the X and Y
 * coordinates in the pattern 0byyyxxxxxxxxx, creating 4k tiles that are 512
 * bytes wide and 8 rows high:
 *
 *   tile(x_tiled, X, Y, S) = A
 *     where A = tile_num << 12 | offset
 *           tile_num = (Y' >> 3) * tile_pitch + (X' >> 9)
 *           offset = (Y' & 0b111) << 9
 *                    | (X & 0b111111111)
 *           X' = X * cpp
 *           Y' = Y + S * qpitch
 *   detile(x_tiled, A) = (X, Y, S)
 *     where X = X' / cpp
 *           Y = Y' % qpitch
 *           S = Y' / qpitch
 *           Y' = (tile_num / tile_pitch) << 3
 *                | (A & 0b111000000000) >> 9
 *           X' = (tile_num % tile_pitch) << 9
 *                | (A & 0b111111111)
 *
 * (In all tiling formulas, cpp is the number of bytes occupied by a single
 * sample ("chars per pixel"), tile_pitch is the number of 4k tiles required
 * to fill the width of the surface, and qpitch is the spacing (in rows)
 * between array slices).
 *
 * For Y tiling, tile() combines together the low-order bits of the X and Y
 * coordinates in the pattern 0bxxxyyyyyxxxx, creating 4k tiles that are 128
 * bytes wide and 32 rows high:
 *
 *   tile(y_tiled, X, Y, S) = A
 *     where A = tile_num << 12 | offset
 *           tile_num = (Y' >> 5) * tile_pitch + (X' >> 7)
 *           offset = (X' & 0b1110000) << 5
 *                    | (Y' & 0b11111) << 4
 *                    | (X' & 0b1111)
 *           X' = X * cpp
 *           Y' = Y + S * qpitch
 *   detile(y_tiled, A) = (X, Y, S)
 *     where X = X' / cpp
 *           Y = Y' % qpitch
 *           S = Y' / qpitch
 *           Y' = (tile_num / tile_pitch) << 5
 *                | (A & 0b111110000) >> 4
 *           X' = (tile_num % tile_pitch) << 7
 *                | (A & 0b111000000000) >> 5
 *                | (A & 0b1111)
 *
 * For W tiling, tile() combines together the low-order bits of the X and Y
 * coordinates in the pattern 0bxxxyyyyxyxyx, creating 4k tiles that are 64
 * bytes wide and 64 rows high (note that W tiling is only used for stencil
 * buffers, which always have cpp = 1 and S=0):
 *
 *   tile(w_tiled, X, Y, S) = A
 *     where A = tile_num << 12 | offset
 *           tile_num = (Y' >> 6) * tile_pitch + (X' >> 6)
 *           offset = (X' & 0b111000) << 6
 *                    | (Y' & 0b111100) << 3
 *                    | (X' & 0b100) << 2
 *                    | (Y' & 0b10) << 2
 *                    | (X' & 0b10) << 1
 *                    | (Y' & 0b1) << 1
 *                    | (X' & 0b1)
 *           X' = X * cpp = X
 *           Y' = Y + S * qpitch
 *   detile(w_tiled, A) = (X, Y, S)
 *     where X = X' / cpp = X'
 *           Y = Y' % qpitch = Y'
 *           S = Y / qpitch = 0
 *           Y' = (tile_num / tile_pitch) << 6
 *                | (A & 0b111100000) >> 3
 *                | (A & 0b1000) >> 2
 *                | (A & 0b10) >> 1
 *           X' = (tile_num % tile_pitch) << 6
 *                | (A & 0b111000000000) >> 6
 *                | (A & 0b10000) >> 2
 *                | (A & 0b100) >> 1
 *                | (A & 0b1)
 *
 * Finally, for a non-tiled surface, tile() simply combines together the X and
 * Y coordinates in the natural way:
 *
 *   tile(untiled, X, Y, S) = A
 *     where A = Y * pitch + X'
 *           X' = X * cpp
 *           Y' = Y + S * qpitch
 *   detile(untiled, A) = (X, Y, S)
 *     where X = X' / cpp
 *           Y = Y' % qpitch
 *           S = Y' / qpitch
 *           X' = A % pitch
 *           Y' = A / pitch
 *
 * (In these formulas, pitch is the number of bytes occupied by a single row
 * of samples).
 */
static nir_shader *
brw_blorp_build_nir_shader(struct brw_context *brw,
                           const brw_blorp_blit_prog_key *key)
{
   nir_ssa_def *src_pos, *dst_pos, *color;

   /* Sanity checks */
   if (key->dst_tiled_w && key->rt_samples > 0) {
      /* If the destination image is W tiled and multisampled, then the thread
       * must be dispatched once per sample, not once per pixel.  This is
       * necessary because after conversion between W and Y tiling, there's no
       * guarantee that all samples corresponding to a single pixel will still
       * be together.
       */
      assert(key->persample_msaa_dispatch);
   }

   if (key->blend) {
      /* We are blending, which means we won't have an opportunity to
       * translate the tiling and sample count for the texture surface.  So
       * the surface state for the texture must be configured with the correct
       * tiling and sample count.
       */
      assert(!key->src_tiled_w);
      assert(key->tex_samples == key->src_samples);
      assert(key->tex_layout == key->src_layout);
      assert(key->tex_samples > 0);
   }

   if (key->persample_msaa_dispatch) {
      /* It only makes sense to do persample dispatch if the render target is
       * configured as multisampled.
       */
      assert(key->rt_samples > 0);
   }

   /* Make sure layout is consistent with sample count */
   assert((key->tex_layout == INTEL_MSAA_LAYOUT_NONE) ==
          (key->tex_samples == 0));
   assert((key->rt_layout == INTEL_MSAA_LAYOUT_NONE) ==
          (key->rt_samples == 0));
   assert((key->src_layout == INTEL_MSAA_LAYOUT_NONE) ==
          (key->src_samples == 0));
   assert((key->dst_layout == INTEL_MSAA_LAYOUT_NONE) ==
          (key->dst_samples == 0));

   nir_builder b;
   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);

   struct brw_blorp_blit_vars v;
   brw_blorp_blit_vars_init(&b, &v, key);

   dst_pos = blorp_blit_get_frag_coords(&b, key, &v);

   /* Render target and texture hardware don't support W tiling until Gen8. */
   const bool rt_tiled_w = false;
   const bool tex_tiled_w = brw->gen >= 8 && key->src_tiled_w;

   /* The address that data will be written to is determined by the
    * coordinates supplied to the WM thread and the tiling and sample count of
    * the render target, according to the formula:
    *
    * (X, Y, S) = decode_msaa(rt_samples, detile(rt_tiling, offset))
    *
    * If the actual tiling and sample count of the destination surface are not
    * the same as the configuration of the render target, then these
    * coordinates are wrong and we have to adjust them to compensate for the
    * difference.
    */
   if (rt_tiled_w != key->dst_tiled_w ||
       key->rt_samples != key->dst_samples ||
       key->rt_layout != key->dst_layout) {
      dst_pos = blorp_nir_encode_msaa(&b, dst_pos, key->rt_samples,
                                      key->rt_layout);
      /* Now (X, Y, S) = detile(rt_tiling, offset) */
      if (rt_tiled_w != key->dst_tiled_w)
         dst_pos = blorp_nir_retile_y_to_w(&b, dst_pos);
      /* Now (X, Y, S) = detile(rt_tiling, offset) */
      dst_pos = blorp_nir_decode_msaa(&b, dst_pos, key->dst_samples,
                                      key->dst_layout);
   }

   /* Now (X, Y, S) = decode_msaa(dst_samples, detile(dst_tiling, offset)).
    *
    * That is: X, Y and S now contain the true coordinates and sample index of
    * the data that the WM thread should output.
    *
    * If we need to kill pixels that are outside the destination rectangle,
    * now is the time to do it.
    */
   if (key->use_kill)
      blorp_nir_discard_if_outside_rect(&b, dst_pos, &v);

   src_pos = blorp_blit_apply_transform(&b, nir_i2f(&b, dst_pos), &v);
   if (dst_pos->num_components == 3) {
      /* The sample coordinate is an integer that we want left alone but
       * blorp_blit_apply_transform() blindly applies the transform to all
       * three coordinates.  Grab the original sample index.
       */
      src_pos = nir_vec3(&b, nir_channel(&b, src_pos, 0),
                             nir_channel(&b, src_pos, 1),
                             nir_channel(&b, dst_pos, 2));
   }

   /* If the source image is not multisampled, then we want to fetch sample
    * number 0, because that's the only sample there is.
    */
   if (key->src_samples == 0)
      src_pos = nir_channels(&b, src_pos, 0x3);

   /* X, Y, and S are now the coordinates of the pixel in the source image
    * that we want to texture from.  Exception: if we are blending, then S is
    * irrelevant, because we are going to fetch all samples.
    */
   if (key->blend && !key->blit_scaled) {
      /* Resolves (effecively) use texelFetch, so we need integers and we
       * don't care about the sample index if we got one.
       */
      src_pos = nir_f2i(&b, nir_channels(&b, src_pos, 0x3));

      if (brw->gen == 6) {
         /* Because gen6 only supports 4x interleved MSAA, we can do all the
          * blending we need with a single linear-interpolated texture lookup
          * at the center of the sample. The texture coordinates to be odd
          * integers so that they correspond to the center of a 2x2 block
          * representing the four samples that maxe up a pixel.  So we need
          * to multiply our X and Y coordinates each by 2 and then add 1.
          */
         src_pos = nir_ishl(&b, src_pos, nir_imm_int(&b, 1));
         src_pos = nir_iadd(&b, src_pos, nir_imm_int(&b, 1));
         src_pos = nir_i2f(&b, src_pos);
         color = blorp_nir_tex(&b, src_pos, key->texture_data_type);
      } else {
         /* Gen7+ hardware doesn't automaticaly blend. */
         color = blorp_nir_manual_blend_average(&b, src_pos, key->src_samples,
                                                key->src_layout,
                                                key->texture_data_type);
      }
   } else if (key->blend && key->blit_scaled) {
      color = blorp_nir_manual_blend_bilinear(&b, src_pos, key->src_samples, key, &v);
   } else {
      if (key->bilinear_filter) {
         color = blorp_nir_tex(&b, src_pos, key->texture_data_type);
      } else {
         /* We're going to use texelFetch, so we need integers */
         if (src_pos->num_components == 2) {
            src_pos = nir_f2i(&b, src_pos);
         } else {
            assert(src_pos->num_components == 3);
            src_pos = nir_vec3(&b, nir_channel(&b, nir_f2i(&b, src_pos), 0),
                                   nir_channel(&b, nir_f2i(&b, src_pos), 1),
                                   nir_channel(&b, src_pos, 2));
         }

         /* We aren't blending, which means we just want to fetch a single
          * sample from the source surface.  The address that we want to fetch
          * from is related to the X, Y and S values according to the formula:
          *
          * (X, Y, S) = decode_msaa(src_samples, detile(src_tiling, offset)).
          *
          * If the actual tiling and sample count of the source surface are
          * not the same as the configuration of the texture, then we need to
          * adjust the coordinates to compensate for the difference.
          */
         if (tex_tiled_w != key->src_tiled_w ||
             key->tex_samples != key->src_samples ||
             key->tex_layout != key->src_layout) {
            src_pos = blorp_nir_encode_msaa(&b, src_pos, key->src_samples,
                                            key->src_layout);
            /* Now (X, Y, S) = detile(src_tiling, offset) */
            if (tex_tiled_w != key->src_tiled_w)
               src_pos = blorp_nir_retile_w_to_y(&b, src_pos);
            /* Now (X, Y, S) = detile(tex_tiling, offset) */
            src_pos = blorp_nir_decode_msaa(&b, src_pos, key->tex_samples,
                                            key->tex_layout);
         }

         /* Now (X, Y, S) = decode_msaa(tex_samples, detile(tex_tiling, offset)).
          *
          * In other words: X, Y, and S now contain values which, when passed to
          * the texturing unit, will cause data to be read from the correct
          * memory location.  So we can fetch the texel now.
          */
         if (key->src_samples == 0) {
            color = blorp_nir_txf(&b, &v, src_pos, key->texture_data_type);
         } else {
            nir_ssa_def *mcs = NULL;
            if (key->tex_layout == INTEL_MSAA_LAYOUT_CMS)
               mcs = blorp_nir_txf_ms_mcs(&b, src_pos);

            color = blorp_nir_txf_ms(&b, src_pos, mcs, key->texture_data_type);
         }
      }
   }

   nir_store_var(&b, v.color_out, color, 0xf);

   return b.shader;
}

static void
brw_blorp_get_blit_kernel(struct brw_context *brw,
                          struct brw_blorp_params *params,
                          const struct brw_blorp_blit_prog_key *prog_key)
{
   if (brw_search_cache(&brw->cache, BRW_CACHE_BLORP_PROG,
                        prog_key, sizeof(*prog_key),
                        &params->wm_prog_kernel, &params->wm_prog_data))
      return;

   const unsigned *program;
   unsigned program_size;
   struct brw_blorp_prog_data prog_data;

   /* Try and compile with NIR first.  If that fails, fall back to the old
    * method of building shaders manually.
    */
   nir_shader *nir = brw_blorp_build_nir_shader(brw, prog_key);
   struct brw_wm_prog_key wm_key;
   brw_blorp_init_wm_prog_key(&wm_key);
   wm_key.tex.compressed_multisample_layout_mask =
      prog_key->tex_layout == INTEL_MSAA_LAYOUT_CMS;
   wm_key.tex.msaa_16 = prog_key->tex_samples == 16;
   wm_key.multisample_fbo = prog_key->rt_samples > 1;

   program = brw_blorp_compile_nir_shader(brw, nir, &wm_key, false,
                                          &prog_data, &program_size);

   brw_upload_cache(&brw->cache, BRW_CACHE_BLORP_PROG,
                    prog_key, sizeof(*prog_key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &params->wm_prog_kernel, &params->wm_prog_data);
}

static void
brw_blorp_setup_coord_transform(struct brw_blorp_coord_transform *xform,
                                GLfloat src0, GLfloat src1,
                                GLfloat dst0, GLfloat dst1,
                                bool mirror)
{
   float scale = (src1 - src0) / (dst1 - dst0);
   if (!mirror) {
      /* When not mirroring a coordinate (say, X), we need:
       *   src_x - src_x0 = (dst_x - dst_x0 + 0.5) * scale
       * Therefore:
       *   src_x = src_x0 + (dst_x - dst_x0 + 0.5) * scale
       *
       * blorp program uses "round toward zero" to convert the
       * transformed floating point coordinates to integer coordinates,
       * whereas the behaviour we actually want is "round to nearest",
       * so 0.5 provides the necessary correction.
       */
      xform->multiplier = scale;
      xform->offset = src0 + (-dst0 + 0.5f) * scale;
   } else {
      /* When mirroring X we need:
       *   src_x - src_x0 = dst_x1 - dst_x - 0.5
       * Therefore:
       *   src_x = src_x0 + (dst_x1 -dst_x - 0.5) * scale
       */
      xform->multiplier = -scale;
      xform->offset = src0 + (dst1 - 0.5f) * scale;
   }
}


/**
 * Determine which MSAA layout the GPU pipeline should be configured for,
 * based on the chip generation, the number of samples, and the true layout of
 * the image in memory.
 */
inline intel_msaa_layout
compute_msaa_layout_for_pipeline(struct brw_context *brw, unsigned num_samples,
                                 intel_msaa_layout true_layout)
{
   if (num_samples <= 1) {
      /* Layout is used to determine if ld2dms is needed for sampling. In
       * single sampled case normal ld is enough avoiding also the need to
       * fetch mcs. Therefore simply set the layout to none.
       */
      if (brw->gen >= 9 && true_layout == INTEL_MSAA_LAYOUT_CMS) {
         return INTEL_MSAA_LAYOUT_NONE;
      }

      /* When configuring the GPU for non-MSAA, we can still accommodate IMS
       * format buffers, by transforming coordinates appropriately.
       */
      assert(true_layout == INTEL_MSAA_LAYOUT_NONE ||
             true_layout == INTEL_MSAA_LAYOUT_IMS);
      return INTEL_MSAA_LAYOUT_NONE;
   } else {
      assert(true_layout != INTEL_MSAA_LAYOUT_NONE);
   }

   /* Prior to Gen7, all MSAA surfaces use IMS layout. */
   if (brw->gen == 6) {
      assert(true_layout == INTEL_MSAA_LAYOUT_IMS);
   }

   return true_layout;
}


/**
 * Note: if the src (or dst) is a 2D multisample array texture on Gen7+ using
 * INTEL_MSAA_LAYOUT_UMS or INTEL_MSAA_LAYOUT_CMS, src_layer (dst_layer) is
 * the physical layer holding sample 0.  So, for example, if
 * src_mt->num_samples == 4, then logical layer n corresponds to src_layer ==
 * 4*n.
 */
void
brw_blorp_blit_miptrees(struct brw_context *brw,
                        struct intel_mipmap_tree *src_mt,
                        unsigned src_level, unsigned src_layer,
                        mesa_format src_format, int src_swizzle,
                        struct intel_mipmap_tree *dst_mt,
                        unsigned dst_level, unsigned dst_layer,
                        mesa_format dst_format,
                        float src_x0, float src_y0,
                        float src_x1, float src_y1,
                        float dst_x0, float dst_y0,
                        float dst_x1, float dst_y1,
                        GLenum filter, bool mirror_x, bool mirror_y,
                        bool decode_srgb, bool encode_srgb)
{
   /* Get ready to blit.  This includes depth resolving the src and dst
    * buffers if necessary.  Note: it's not necessary to do a color resolve on
    * the destination buffer because we use the standard render path to render
    * to destination color buffers, and the standard render path is
    * fast-color-aware.
    */
   intel_miptree_resolve_color(brw, src_mt, INTEL_MIPTREE_IGNORE_CCS_E);
   intel_miptree_slice_resolve_depth(brw, src_mt, src_level, src_layer);
   intel_miptree_slice_resolve_depth(brw, dst_mt, dst_level, dst_layer);

   intel_miptree_prepare_mcs(brw, dst_mt);

   DBG("%s from %dx %s mt %p %d %d (%f,%f) (%f,%f)"
       "to %dx %s mt %p %d %d (%f,%f) (%f,%f) (flip %d,%d)\n",
       __func__,
       src_mt->num_samples, _mesa_get_format_name(src_mt->format), src_mt,
       src_level, src_layer, src_x0, src_y0, src_x1, src_y1,
       dst_mt->num_samples, _mesa_get_format_name(dst_mt->format), dst_mt,
       dst_level, dst_layer, dst_x0, dst_y0, dst_x1, dst_y1,
       mirror_x, mirror_y);

   if (!decode_srgb && _mesa_get_format_color_encoding(src_format) == GL_SRGB)
      src_format = _mesa_get_srgb_format_linear(src_format);

   if (!encode_srgb && _mesa_get_format_color_encoding(dst_format) == GL_SRGB)
      dst_format = _mesa_get_srgb_format_linear(dst_format);

   struct brw_blorp_params params;
   brw_blorp_params_init(&params);

   brw_blorp_surface_info_init(brw, &params.src, src_mt, src_level,
                               src_layer, src_format, false);
   brw_blorp_surface_info_init(brw, &params.dst, dst_mt, dst_level,
                               dst_layer, dst_format, true);

   /* Even though we do multisample resolves at the time of the blit, OpenGL
    * specification defines them as if they happen at the time of rendering,
    * which means that the type of averaging we do during the resolve should
    * only depend on the source format; the destination format should be
    * ignored. But, specification doesn't seem to be strict about it.
    *
    * It has been observed that mulitisample resolves produce slightly better
    * looking images when averaging is done using destination format. NVIDIA's
    * proprietary OpenGL driver also follow this approach. So, we choose to
    * follow it in our driver.
    *
    * When multisampling, if the source and destination formats are equal
    * (aside from the color space), we choose to blit in sRGB space to get
    * this higher quality image.
    */
   if (params.src.num_samples > 1 &&
       _mesa_get_format_color_encoding(dst_mt->format) == GL_SRGB &&
       _mesa_get_srgb_format_linear(src_mt->format) ==
       _mesa_get_srgb_format_linear(dst_mt->format)) {
      assert(brw->format_supported_as_render_target[dst_mt->format]);
      params.dst.brw_surfaceformat = brw->render_target_format[dst_mt->format];
      params.src.brw_surfaceformat = brw_format_for_mesa_format(dst_mt->format);
   }

   /* When doing a multisample resolve of a GL_LUMINANCE32F or GL_INTENSITY32F
    * texture, the above code configures the source format for L32_FLOAT or
    * I32_FLOAT, and the destination format for R32_FLOAT.  On Sandy Bridge,
    * the SAMPLE message appears to handle multisampled L32_FLOAT and
    * I32_FLOAT textures incorrectly, resulting in blocky artifacts.  So work
    * around the problem by using a source format of R32_FLOAT.  This
    * shouldn't affect rendering correctness, since the destination format is
    * R32_FLOAT, so only the contents of the red channel matters.
    */
   if (brw->gen == 6 &&
       params.src.num_samples > 1 && params.dst.num_samples <= 1 &&
       src_mt->format == dst_mt->format &&
       params.dst.brw_surfaceformat == BRW_SURFACEFORMAT_R32_FLOAT) {
      params.src.brw_surfaceformat = params.dst.brw_surfaceformat;
   }

   struct brw_blorp_blit_prog_key wm_prog_key;
   memset(&wm_prog_key, 0, sizeof(wm_prog_key));

   /* texture_data_type indicates the register type that should be used to
    * manipulate texture data.
    */
   switch (_mesa_get_format_datatype(src_mt->format)) {
   case GL_UNSIGNED_NORMALIZED:
   case GL_SIGNED_NORMALIZED:
   case GL_FLOAT:
      wm_prog_key.texture_data_type = BRW_REGISTER_TYPE_F;
      break;
   case GL_UNSIGNED_INT:
      if (src_mt->format == MESA_FORMAT_S_UINT8) {
         /* We process stencil as though it's an unsigned normalized color */
         wm_prog_key.texture_data_type = BRW_REGISTER_TYPE_F;
      } else {
         wm_prog_key.texture_data_type = BRW_REGISTER_TYPE_UD;
      }
      break;
   case GL_INT:
      wm_prog_key.texture_data_type = BRW_REGISTER_TYPE_D;
      break;
   default:
      unreachable("Unrecognized blorp format");
   }

   if (brw->gen > 6) {
      /* Gen7's rendering hardware only supports the IMS layout for depth and
       * stencil render targets.  Blorp always maps its destination surface as
       * a color render target (even if it's actually a depth or stencil
       * buffer).  So if the destination is IMS, we'll have to map it as a
       * single-sampled texture and interleave the samples ourselves.
       */
      if (dst_mt->msaa_layout == INTEL_MSAA_LAYOUT_IMS)
         params.dst.num_samples = 0;
   }

   if (params.dst.map_stencil_as_y_tiled && params.dst.num_samples > 1) {
      /* If the destination surface is a W-tiled multisampled stencil buffer
       * that we're mapping as Y tiled, then we need to arrange for the WM
       * program to run once per sample rather than once per pixel, because
       * the memory layout of related samples doesn't match between W and Y
       * tiling.
       */
      wm_prog_key.persample_msaa_dispatch = true;
   }

   if (params.src.num_samples > 0 && params.dst.num_samples > 1) {
      /* We are blitting from a multisample buffer to a multisample buffer, so
       * we must preserve samples within a pixel.  This means we have to
       * arrange for the WM program to run once per sample rather than once
       * per pixel.
       */
      wm_prog_key.persample_msaa_dispatch = true;
   }

   /* Scaled blitting or not. */
   wm_prog_key.blit_scaled =
      ((dst_x1 - dst_x0) == (src_x1 - src_x0) &&
       (dst_y1 - dst_y0) == (src_y1 - src_y0)) ? false : true;

   /* Scaling factors used for bilinear filtering in multisample scaled
    * blits.
    */
   if (src_mt->num_samples == 16)
      wm_prog_key.x_scale = 4.0f;
   else
      wm_prog_key.x_scale = 2.0f;
   wm_prog_key.y_scale = src_mt->num_samples / wm_prog_key.x_scale;

   if (filter == GL_LINEAR &&
       params.src.num_samples <= 1 && params.dst.num_samples <= 1)
      wm_prog_key.bilinear_filter = true;

   GLenum base_format = _mesa_get_format_base_format(src_mt->format);
   if (base_format != GL_DEPTH_COMPONENT && /* TODO: what about depth/stencil? */
       base_format != GL_STENCIL_INDEX &&
       !_mesa_is_format_integer(src_mt->format) &&
       src_mt->num_samples > 1 && dst_mt->num_samples <= 1) {
      /* We are downsampling a non-integer color buffer, so blend.
       *
       * Regarding integer color buffers, the OpenGL ES 3.2 spec says:
       *
       *    "If the source formats are integer types or stencil values, a
       *    single sample's value is selected for each pixel."
       *
       * This implies we should not blend in that case.
       */
      wm_prog_key.blend = true;
   }

   /* src_samples and dst_samples are the true sample counts */
   wm_prog_key.src_samples = src_mt->num_samples;
   wm_prog_key.dst_samples = dst_mt->num_samples;

   /* tex_samples and rt_samples are the sample counts that are set up in
    * SURFACE_STATE.
    */
   wm_prog_key.tex_samples = params.src.num_samples;
   wm_prog_key.rt_samples  = params.dst.num_samples;

   /* tex_layout and rt_layout indicate the MSAA layout the GPU pipeline will
    * use to access the source and destination surfaces.
    */
   wm_prog_key.tex_layout =
      compute_msaa_layout_for_pipeline(brw, params.src.num_samples,
                                       params.src.msaa_layout);
   wm_prog_key.rt_layout =
      compute_msaa_layout_for_pipeline(brw, params.dst.num_samples,
                                       params.dst.msaa_layout);

   /* src_layout and dst_layout indicate the true MSAA layout used by src and
    * dst.
    */
   wm_prog_key.src_layout = src_mt->msaa_layout;
   wm_prog_key.dst_layout = dst_mt->msaa_layout;

   /* On gen9+ compressed single sampled buffers carry the same layout type as
    * multisampled. The difference is that they can be sampled using normal
    * ld message and as render target behave just like non-compressed surface
    * from compiler point of view. Therefore override the type in the program
    * key.
    */
   if (brw->gen >= 9 && params.src.num_samples <= 1 &&
       src_mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS)
      wm_prog_key.src_layout = INTEL_MSAA_LAYOUT_NONE;
   if (brw->gen >= 9 && params.dst.num_samples <= 1 &&
       dst_mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS)
      wm_prog_key.dst_layout = INTEL_MSAA_LAYOUT_NONE;

   wm_prog_key.src_tiled_w = params.src.map_stencil_as_y_tiled;
   wm_prog_key.dst_tiled_w = params.dst.map_stencil_as_y_tiled;
   /* Round floating point values to nearest integer to avoid "off by one texel"
    * kind of errors when blitting.
    */
   params.x0 = params.wm_push_consts.dst_x0 = roundf(dst_x0);
   params.y0 = params.wm_push_consts.dst_y0 = roundf(dst_y0);
   params.x1 = params.wm_push_consts.dst_x1 = roundf(dst_x1);
   params.y1 = params.wm_push_consts.dst_y1 = roundf(dst_y1);
   params.wm_push_consts.rect_grid_x1 =
      minify(src_mt->logical_width0, src_level) * wm_prog_key.x_scale - 1.0f;
   params.wm_push_consts.rect_grid_y1 =
      minify(src_mt->logical_height0, src_level) * wm_prog_key.y_scale - 1.0f;

   brw_blorp_setup_coord_transform(&params.wm_push_consts.x_transform,
                                   src_x0, src_x1, dst_x0, dst_x1, mirror_x);
   brw_blorp_setup_coord_transform(&params.wm_push_consts.y_transform,
                                   src_y0, src_y1, dst_y0, dst_y1, mirror_y);

   params.wm_push_consts.src_z =
      params.src.mt->target == GL_TEXTURE_3D ? params.src.layer : 0;

   if (params.dst.num_samples <= 1 && dst_mt->num_samples > 1) {
      /* We must expand the rectangle we send through the rendering pipeline,
       * to account for the fact that we are mapping the destination region as
       * single-sampled when it is in fact multisampled.  We must also align
       * it to a multiple of the multisampling pattern, because the
       * differences between multisampled and single-sampled surface formats
       * will mean that pixels are scrambled within the multisampling pattern.
       * TODO: what if this makes the coordinates too large?
       *
       * Note: this only works if the destination surface uses the IMS layout.
       * If it's UMS, then we have no choice but to set up the rendering
       * pipeline as multisampled.
       */
      assert(dst_mt->msaa_layout == INTEL_MSAA_LAYOUT_IMS);
      switch (dst_mt->num_samples) {
      case 2:
         params.x0 = ROUND_DOWN_TO(params.x0 * 2, 4);
         params.y0 = ROUND_DOWN_TO(params.y0, 4);
         params.x1 = ALIGN(params.x1 * 2, 4);
         params.y1 = ALIGN(params.y1, 4);
         break;
      case 4:
         params.x0 = ROUND_DOWN_TO(params.x0 * 2, 4);
         params.y0 = ROUND_DOWN_TO(params.y0 * 2, 4);
         params.x1 = ALIGN(params.x1 * 2, 4);
         params.y1 = ALIGN(params.y1 * 2, 4);
         break;
      case 8:
         params.x0 = ROUND_DOWN_TO(params.x0 * 4, 8);
         params.y0 = ROUND_DOWN_TO(params.y0 * 2, 4);
         params.x1 = ALIGN(params.x1 * 4, 8);
         params.y1 = ALIGN(params.y1 * 2, 4);
         break;
      case 16:
         params.x0 = ROUND_DOWN_TO(params.x0 * 4, 8);
         params.y0 = ROUND_DOWN_TO(params.y0 * 4, 8);
         params.x1 = ALIGN(params.x1 * 4, 8);
         params.y1 = ALIGN(params.y1 * 4, 8);
         break;
      default:
         unreachable("Unrecognized sample count in brw_blorp_blit_params ctor");
      }
      wm_prog_key.use_kill = true;
   }

   if (params.dst.map_stencil_as_y_tiled) {
      /* We must modify the rectangle we send through the rendering pipeline
       * (and the size and x/y offset of the destination surface), to account
       * for the fact that we are mapping it as Y-tiled when it is in fact
       * W-tiled.
       *
       * Both Y tiling and W tiling can be understood as organizations of
       * 32-byte sub-tiles; within each 32-byte sub-tile, the layout of pixels
       * is different, but the layout of the 32-byte sub-tiles within the 4k
       * tile is the same (8 sub-tiles across by 16 sub-tiles down, in
       * column-major order).  In Y tiling, the sub-tiles are 16 bytes wide
       * and 2 rows high; in W tiling, they are 8 bytes wide and 4 rows high.
       *
       * Therefore, to account for the layout differences within the 32-byte
       * sub-tiles, we must expand the rectangle so the X coordinates of its
       * edges are multiples of 8 (the W sub-tile width), and its Y
       * coordinates of its edges are multiples of 4 (the W sub-tile height).
       * Then we need to scale the X and Y coordinates of the rectangle to
       * account for the differences in aspect ratio between the Y and W
       * sub-tiles.  We need to modify the layer width and height similarly.
       *
       * A correction needs to be applied when MSAA is in use: since
       * INTEL_MSAA_LAYOUT_IMS uses an interleaving pattern whose height is 4,
       * we need to align the Y coordinates to multiples of 8, so that when
       * they are divided by two they are still multiples of 4.
       *
       * Note: Since the x/y offset of the surface will be applied using the
       * SURFACE_STATE command packet, it will be invisible to the swizzling
       * code in the shader; therefore it needs to be in a multiple of the
       * 32-byte sub-tile size.  Fortunately it is, since the sub-tile is 8
       * pixels wide and 4 pixels high (when viewed as a W-tiled stencil
       * buffer), and the miplevel alignment used for stencil buffers is 8
       * pixels horizontally and either 4 or 8 pixels vertically (see
       * intel_horizontal_texture_alignment_unit() and
       * intel_vertical_texture_alignment_unit()).
       *
       * Note: Also, since the SURFACE_STATE command packet can only apply
       * offsets that are multiples of 4 pixels horizontally and 2 pixels
       * vertically, it is important that the offsets will be multiples of
       * these sizes after they are converted into Y-tiled coordinates.
       * Fortunately they will be, since we know from above that the offsets
       * are a multiple of the 32-byte sub-tile size, and in Y-tiled
       * coordinates the sub-tile is 16 pixels wide and 2 pixels high.
       *
       * TODO: what if this makes the coordinates (or the texture size) too
       * large?
       */
      const unsigned x_align = 8, y_align = params.dst.num_samples != 0 ? 8 : 4;
      params.x0 = ROUND_DOWN_TO(params.x0, x_align) * 2;
      params.y0 = ROUND_DOWN_TO(params.y0, y_align) / 2;
      params.x1 = ALIGN(params.x1, x_align) * 2;
      params.y1 = ALIGN(params.y1, y_align) / 2;
      params.dst.width = ALIGN(params.dst.width, x_align) * 2;
      params.dst.height = ALIGN(params.dst.height, y_align) / 2;
      params.dst.x_offset *= 2;
      params.dst.y_offset /= 2;
      wm_prog_key.use_kill = true;
   }

   if (params.src.map_stencil_as_y_tiled) {
      /* We must modify the size and x/y offset of the source surface to
       * account for the fact that we are mapping it as Y-tiled when it is in
       * fact W tiled.
       *
       * See the comments above concerning x/y offset alignment for the
       * destination surface.
       *
       * TODO: what if this makes the texture size too large?
       */
      const unsigned x_align = 8, y_align = params.src.num_samples != 0 ? 8 : 4;
      params.src.width = ALIGN(params.src.width, x_align) * 2;
      params.src.height = ALIGN(params.src.height, y_align) / 2;
      params.src.x_offset *= 2;
      params.src.y_offset /= 2;
   }

   brw_blorp_get_blit_kernel(brw, &params, &wm_prog_key);

   params.src.swizzle = src_swizzle;

   brw_blorp_exec(brw, &params);

   intel_miptree_slice_set_needs_hiz_resolve(dst_mt, dst_level, dst_layer);

   if (intel_miptree_is_lossless_compressed(brw, dst_mt))
      dst_mt->fast_clear_state = INTEL_FAST_CLEAR_STATE_UNRESOLVED;
}
