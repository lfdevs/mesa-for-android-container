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

#include "compiler/nir/nir_builder.h"

#include "blorp_priv.h"
#include "brw_meta_util.h"

/* header-only include needed for _mesa_unorm_to_float and friends. */
#include "mesa/main/format_utils.h"

#define FILE_DEBUG_FLAG DEBUG_BLORP

static const bool split_blorp_blit_debug = false;

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
   /* Input values from brw_blorp_wm_inputs */
   nir_variable *v_discard_rect;
   nir_variable *v_rect_grid;
   nir_variable *v_coord_transform;
   nir_variable *v_src_z;
   nir_variable *v_src_offset;
   nir_variable *v_dst_offset;

   /* gl_FragCoord */
   nir_variable *frag_coord;

   /* gl_FragColor */
   nir_variable *color_out;
};

static void
brw_blorp_blit_vars_init(nir_builder *b, struct brw_blorp_blit_vars *v,
                         const struct brw_blorp_blit_prog_key *key)
{
    /* Blended and scaled blits never use pixel discard. */
    assert(!key->use_kill || !(key->blend && key->blit_scaled));

#define LOAD_INPUT(name, type)\
   v->v_##name = BLORP_CREATE_NIR_INPUT(b->shader, name, type);

   LOAD_INPUT(discard_rect, glsl_vec4_type())
   LOAD_INPUT(rect_grid, glsl_vec4_type())
   LOAD_INPUT(coord_transform, glsl_vec4_type())
   LOAD_INPUT(src_z, glsl_uint_type())
   LOAD_INPUT(src_offset, glsl_vector_type(GLSL_TYPE_UINT, 2))
   LOAD_INPUT(dst_offset, glsl_vector_type(GLSL_TYPE_UINT, 2))

#undef LOAD_INPUT

   v->frag_coord = nir_variable_create(b->shader, nir_var_shader_in,
                                       glsl_vec4_type(), "gl_FragCoord");
   v->frag_coord->data.location = VARYING_SLOT_POS;
   v->frag_coord->data.origin_upper_left = true;

   v->color_out = nir_variable_create(b->shader, nir_var_shader_out,
                                      glsl_vec4_type(), "gl_FragColor");
   v->color_out->data.location = FRAG_RESULT_COLOR;
}

static nir_ssa_def *
blorp_blit_get_frag_coords(nir_builder *b,
                           const struct brw_blorp_blit_prog_key *key,
                           struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *coord = nir_f2i(b, nir_load_var(b, v->frag_coord));

   /* Account for destination surface intratile offset
    *
    * Transformation parameters giving translation from destination to source
    * coordinates don't take into account possible intra-tile destination
    * offset.  Therefore it has to be first subtracted from the incoming
    * coordinates.  Vertices are set up based on coordinates containing the
    * intra-tile offset.
    */
   if (key->need_dst_offset)
      coord = nir_isub(b, coord, nir_load_var(b, v->v_dst_offset));

   if (key->persample_msaa_dispatch) {
      return nir_vec3(b, nir_channel(b, coord, 0), nir_channel(b, coord, 1),
                      nir_load_sample_id(b));
   } else {
      return nir_vec2(b, nir_channel(b, coord, 0), nir_channel(b, coord, 1));
   }
}

/**
 * Emit code to translate from destination (X, Y) coordinates to source (X, Y)
 * coordinates.
 */
static nir_ssa_def *
blorp_blit_apply_transform(nir_builder *b, nir_ssa_def *src_pos,
                           struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *coord_transform = nir_load_var(b, v->v_coord_transform);

   nir_ssa_def *offset = nir_vec2(b, nir_channel(b, coord_transform, 1),
                                     nir_channel(b, coord_transform, 3));
   nir_ssa_def *mul = nir_vec2(b, nir_channel(b, coord_transform, 0),
                                  nir_channel(b, coord_transform, 2));

   return nir_ffma(b, src_pos, mul, offset);
}

static inline void
blorp_nir_discard_if_outside_rect(nir_builder *b, nir_ssa_def *pos,
                                  struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *c0, *c1, *c2, *c3;
   nir_ssa_def *discard_rect = nir_load_var(b, v->v_discard_rect);
   nir_ssa_def *dst_x0 = nir_channel(b, discard_rect, 0);
   nir_ssa_def *dst_x1 = nir_channel(b, discard_rect, 1);
   nir_ssa_def *dst_y0 = nir_channel(b, discard_rect, 2);
   nir_ssa_def *dst_y1 = nir_channel(b, discard_rect, 3);

   c0 = nir_ult(b, nir_channel(b, pos, 0), dst_x0);
   c1 = nir_uge(b, nir_channel(b, pos, 0), dst_x1);
   c2 = nir_ult(b, nir_channel(b, pos, 1), dst_y0);
   c3 = nir_uge(b, nir_channel(b, pos, 1), dst_y1);

   nir_ssa_def *oob = nir_ior(b, nir_ior(b, c0, c1), nir_ior(b, c2, c3));

   nir_intrinsic_instr *discard =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard_if);
   discard->src[0] = nir_src_for_ssa(oob);
   nir_builder_instr_insert(b, &discard->instr);
}

static nir_tex_instr *
blorp_create_nir_tex_instr(nir_builder *b, struct brw_blorp_blit_vars *v,
                           nir_texop op, nir_ssa_def *pos, unsigned num_srcs,
                           nir_alu_type dst_type)
{
   nir_tex_instr *tex = nir_tex_instr_create(b->shader, num_srcs);

   tex->op = op;

   tex->dest_type = dst_type;
   tex->is_array = false;
   tex->is_shadow = false;

   /* Blorp only has one texture and it's bound at unit 0 */
   tex->texture = NULL;
   tex->sampler = NULL;
   tex->texture_index = 0;
   tex->sampler_index = 0;

   /* To properly handle 3-D and 2-D array textures, we pull the Z component
    * from an input.  TODO: This is a bit magic; we should probably make this
    * more explicit in the future.
    */
   assert(pos->num_components >= 2);
   pos = nir_vec3(b, nir_channel(b, pos, 0), nir_channel(b, pos, 1),
                     nir_load_var(b, v->v_src_z));

   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(pos);
   tex->coord_components = 3;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);

   return tex;
}

static nir_ssa_def *
blorp_nir_tex(nir_builder *b, struct brw_blorp_blit_vars *v,
              nir_ssa_def *pos, nir_alu_type dst_type)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b, v, nir_texop_tex, pos, 2, dst_type);

   assert(pos->num_components == 2);
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));

   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
blorp_nir_txf(nir_builder *b, struct brw_blorp_blit_vars *v,
              nir_ssa_def *pos, nir_alu_type dst_type)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b, v, nir_texop_txf, pos, 2, dst_type);

   tex->sampler_dim = GLSL_SAMPLER_DIM_3D;
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));

   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
blorp_nir_txf_ms(nir_builder *b, struct brw_blorp_blit_vars *v,
                 nir_ssa_def *pos, nir_ssa_def *mcs, nir_alu_type dst_type)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b, v, nir_texop_txf_ms, pos,
                                 mcs != NULL ? 3 : 2, dst_type);

   tex->sampler_dim = GLSL_SAMPLER_DIM_MS;

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
blorp_nir_txf_ms_mcs(nir_builder *b, struct brw_blorp_blit_vars *v, nir_ssa_def *pos)
{
   nir_tex_instr *tex =
      blorp_create_nir_tex_instr(b, v, nir_texop_txf_ms_mcs,
                                 pos, 1, nir_type_int);

   tex->sampler_dim = GLSL_SAMPLER_DIM_MS;

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
                      unsigned num_samples, enum isl_msaa_layout layout)
{
   assert(pos->num_components == 2 || pos->num_components == 3);

   switch (layout) {
   case ISL_MSAA_LAYOUT_NONE:
      assert(pos->num_components == 2);
      return pos;
   case ISL_MSAA_LAYOUT_ARRAY:
      /* No translation needed */
      return pos;
   case ISL_MSAA_LAYOUT_INTERLEAVED: {
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
                      unsigned num_samples, enum isl_msaa_layout layout)
{
   assert(pos->num_components == 2 || pos->num_components == 3);

   switch (layout) {
   case ISL_MSAA_LAYOUT_NONE:
      /* No translation necessary, and S should already be zero. */
      assert(pos->num_components == 2);
      return pos;
   case ISL_MSAA_LAYOUT_ARRAY:
      /* No translation necessary. */
      return pos;
   case ISL_MSAA_LAYOUT_INTERLEAVED: {
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
blorp_nir_manual_blend_average(nir_builder *b, struct brw_blorp_blit_vars *v,
                               nir_ssa_def *pos, unsigned tex_samples,
                               enum isl_aux_usage tex_aux_usage,
                               nir_alu_type dst_type)
{
   /* If non-null, this is the outer-most if statement */
   nir_if *outer_if = NULL;

   nir_variable *color =
      nir_local_variable_create(b->impl, glsl_vec4_type(), "color");

   nir_ssa_def *mcs = NULL;
   if (tex_aux_usage == ISL_AUX_USAGE_MCS)
      mcs = blorp_nir_txf_ms_mcs(b, v, pos);

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
      texture_data[stack_depth++] = blorp_nir_txf_ms(b, v, ms_pos, mcs, dst_type);

      if (i == 0 && tex_aux_usage == ISL_AUX_USAGE_MCS) {
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

         assert(dst_type == nir_type_float);
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
                                const struct brw_blorp_blit_prog_key *key,
                                struct brw_blorp_blit_vars *v)
{
   nir_ssa_def *pos_xy = nir_channels(b, pos, 0x3);
   nir_ssa_def *rect_grid = nir_load_var(b, v->v_rect_grid);
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
                        nir_vec2(b, nir_channel(b, rect_grid, 0),
                                    nir_channel(b, rect_grid, 1)));

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
      if (key->tex_aux_usage == ISL_AUX_USAGE_MCS)
         mcs = blorp_nir_txf_ms_mcs(b, v, sample_coords_int);

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
       *                        | 0 | 1 |                            | 3 | 7 |
       *                        ---------                            ---------
       *                        | 2 | 3 |                            | 5 | 0 |
       *                        ---------                            ---------
       *                        | 4 | 5 |                            | 1 | 2 |
       *                        ---------                            ---------
       *                        | 6 | 7 |                            | 4 | 6 |
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
         sample = nir_iand(b, nir_ishr(b, nir_imm_int(b, 0x64210573),
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
      tex_data[i] = blorp_nir_txf_ms(b, v, pos_ms, mcs, key->texture_data_type);
   }

   nir_ssa_def *frac_x = nir_channel(b, frac_xy, 0);
   nir_ssa_def *frac_y = nir_channel(b, frac_xy, 1);
   return nir_flrp(b, nir_flrp(b, tex_data[0], tex_data[1], frac_x),
                      nir_flrp(b, tex_data[2], tex_data[3], frac_x),
                      frac_y);
}

/** Perform a color bit-cast operation
 *
 * For copy operations involving CCS, we may need to use different formats for
 * the source and destination surfaces.  The two formats must both be UINT
 * formats and must have the same size but may have different bit layouts.
 * For instance, we may be copying from R8G8B8A8_UINT to R32_UINT or R32_UINT
 * to R16G16_UINT.  This function generates code to shuffle bits around to get
 * us from one to the other.
 */
static nir_ssa_def *
bit_cast_color(struct nir_builder *b, nir_ssa_def *color,
               const struct brw_blorp_blit_prog_key *key)
{
   assert(key->texture_data_type == nir_type_uint);

   if (key->dst_bpc > key->src_bpc) {
      nir_ssa_def *u = nir_ssa_undef(b, 1, 32);
      nir_ssa_def *dst_chan[2] = { u, u };
      unsigned shift = 0;
      unsigned dst_idx = 0;
      for (unsigned i = 0; i < 4; i++) {
         nir_ssa_def *shifted = nir_ishl(b, nir_channel(b, color, i),
                                            nir_imm_int(b, shift));
         if (shift == 0) {
            dst_chan[dst_idx] = shifted;
         } else {
            dst_chan[dst_idx] = nir_ior(b, dst_chan[dst_idx], shifted);
         }

         shift += key->src_bpc;
         if (shift >= key->dst_bpc) {
            dst_idx++;
            shift = 0;
         }
      }

      return nir_vec4(b, dst_chan[0], dst_chan[1], u, u);
   } else {
      assert(key->dst_bpc < key->src_bpc);

      nir_ssa_def *mask = nir_imm_int(b, ~0u >> (32 - key->dst_bpc));

      nir_ssa_def *dst_chan[4];
      unsigned src_idx = 0;
      unsigned shift = 0;
      for (unsigned i = 0; i < 4; i++) {
         dst_chan[i] = nir_iand(b, nir_ushr(b, nir_channel(b, color, src_idx),
                                               nir_imm_int(b, shift)),
                                   mask);
         shift += key->dst_bpc;
         if (shift >= key->src_bpc) {
            src_idx++;
            shift = 0;
         }
      }

      return nir_vec4(b, dst_chan[0], dst_chan[1], dst_chan[2], dst_chan[3]);
   }
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
brw_blorp_build_nir_shader(struct blorp_context *blorp, void *mem_ctx,
                           const struct brw_blorp_blit_prog_key *key)
{
   const struct gen_device_info *devinfo = blorp->isl_dev->info;
   nir_ssa_def *src_pos, *dst_pos, *color;

   /* Sanity checks */
   if (key->dst_tiled_w && key->rt_samples > 1) {
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
   assert((key->tex_layout == ISL_MSAA_LAYOUT_NONE) ==
          (key->tex_samples <= 1));
   assert((key->rt_layout == ISL_MSAA_LAYOUT_NONE) ==
          (key->rt_samples <= 1));
   assert((key->src_layout == ISL_MSAA_LAYOUT_NONE) ==
          (key->src_samples <= 1));
   assert((key->dst_layout == ISL_MSAA_LAYOUT_NONE) ==
          (key->dst_samples <= 1));

   nir_builder b;
   nir_builder_init_simple_shader(&b, mem_ctx, MESA_SHADER_FRAGMENT, NULL);

   struct brw_blorp_blit_vars v;
   brw_blorp_blit_vars_init(&b, &v, key);

   dst_pos = blorp_blit_get_frag_coords(&b, key, &v);

   /* Render target and texture hardware don't support W tiling until Gen8. */
   const bool rt_tiled_w = false;
   const bool tex_tiled_w = devinfo->gen >= 8 && key->src_tiled_w;

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
   if (key->use_kill) {
      assert(!(key->blend && key->blit_scaled));
      blorp_nir_discard_if_outside_rect(&b, dst_pos, &v);
   }

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
   if (key->src_samples == 1)
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

      if (devinfo->gen == 6) {
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
         color = blorp_nir_tex(&b, &v, src_pos, key->texture_data_type);
      } else {
         /* Gen7+ hardware doesn't automaticaly blend. */
         color = blorp_nir_manual_blend_average(&b, &v, src_pos, key->src_samples,
                                                key->tex_aux_usage,
                                                key->texture_data_type);
      }
   } else if (key->blend && key->blit_scaled) {
      assert(!key->use_kill);
      color = blorp_nir_manual_blend_bilinear(&b, src_pos, key->src_samples, key, &v);
   } else {
      if (key->bilinear_filter) {
         color = blorp_nir_tex(&b, &v, src_pos, key->texture_data_type);
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

         if (key->need_src_offset)
            src_pos = nir_iadd(&b, src_pos, nir_load_var(&b, v.v_src_offset));

         /* Now (X, Y, S) = decode_msaa(tex_samples, detile(tex_tiling, offset)).
          *
          * In other words: X, Y, and S now contain values which, when passed to
          * the texturing unit, will cause data to be read from the correct
          * memory location.  So we can fetch the texel now.
          */
         if (key->src_samples == 1) {
            color = blorp_nir_txf(&b, &v, src_pos, key->texture_data_type);
         } else {
            nir_ssa_def *mcs = NULL;
            if (key->tex_aux_usage == ISL_AUX_USAGE_MCS)
               mcs = blorp_nir_txf_ms_mcs(&b, &v, src_pos);

            color = blorp_nir_txf_ms(&b, &v, src_pos, mcs, key->texture_data_type);
         }
      }
   }

   if (key->dst_bpc != key->src_bpc)
      color = bit_cast_color(&b, color, key);

   if (key->dst_rgb) {
      /* The destination image is bound as a red texture three times as wide
       * as the actual image.  Our shader is effectively running one color
       * component at a time.  We need to pick off the appropriate component
       * from the source color and write that to destination red.
       */
      assert(dst_pos->num_components == 2);
      nir_ssa_def *comp =
         nir_umod(&b, nir_channel(&b, dst_pos, 0), nir_imm_int(&b, 3));

      nir_ssa_def *color_component =
         nir_bcsel(&b, nir_ieq(&b, comp, nir_imm_int(&b, 0)),
                       nir_channel(&b, color, 0),
                       nir_bcsel(&b, nir_ieq(&b, comp, nir_imm_int(&b, 1)),
                                     nir_channel(&b, color, 1),
                                     nir_channel(&b, color, 2)));

      nir_ssa_def *u = nir_ssa_undef(&b, 1, 32);
      color = nir_vec4(&b, color_component, u, u, u);
   }

   nir_store_var(&b, v.color_out, color, 0xf);

   return b.shader;
}

static void
brw_blorp_get_blit_kernel(struct blorp_context *blorp,
                          struct blorp_params *params,
                          const struct brw_blorp_blit_prog_key *prog_key)
{
   if (blorp->lookup_shader(blorp, prog_key, sizeof(*prog_key),
                            &params->wm_prog_kernel, &params->wm_prog_data))
      return;

   void *mem_ctx = ralloc_context(NULL);

   const unsigned *program;
   unsigned program_size;
   struct brw_wm_prog_data prog_data;

   nir_shader *nir = brw_blorp_build_nir_shader(blorp, mem_ctx, prog_key);
   struct brw_wm_prog_key wm_key;
   brw_blorp_init_wm_prog_key(&wm_key);
   wm_key.tex.compressed_multisample_layout_mask =
      prog_key->tex_aux_usage == ISL_AUX_USAGE_MCS;
   wm_key.tex.msaa_16 = prog_key->tex_samples == 16;
   wm_key.multisample_fbo = prog_key->rt_samples > 1;

   program = blorp_compile_fs(blorp, mem_ctx, nir, &wm_key, false,
                              &prog_data, &program_size);

   blorp->upload_shader(blorp, prog_key, sizeof(*prog_key),
                        program, program_size,
                        &prog_data.base, sizeof(prog_data),
                        &params->wm_prog_kernel, &params->wm_prog_data);

   ralloc_free(mem_ctx);
}

static void
brw_blorp_setup_coord_transform(struct brw_blorp_coord_transform *xform,
                                GLfloat src0, GLfloat src1,
                                GLfloat dst0, GLfloat dst1,
                                bool mirror)
{
   double scale = (double)(src1 - src0) / (double)(dst1 - dst0);
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
      xform->offset = src0 + (-(double)dst0 + 0.5) * scale;
   } else {
      /* When mirroring X we need:
       *   src_x - src_x0 = dst_x1 - dst_x - 0.5
       * Therefore:
       *   src_x = src_x0 + (dst_x1 -dst_x - 0.5) * scale
       */
      xform->multiplier = -scale;
      xform->offset = src0 + ((double)dst1 - 0.5) * scale;
   }
}

static inline void
surf_get_intratile_offset_px(struct brw_blorp_surface_info *info,
                             uint32_t *tile_x_px, uint32_t *tile_y_px)
{
   if (info->surf.msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED) {
      struct isl_extent2d px_size_sa =
         isl_get_interleaved_msaa_px_size_sa(info->surf.samples);
      assert(info->tile_x_sa % px_size_sa.width == 0);
      assert(info->tile_y_sa % px_size_sa.height == 0);
      *tile_x_px = info->tile_x_sa / px_size_sa.width;
      *tile_y_px = info->tile_y_sa / px_size_sa.height;
   } else {
      *tile_x_px = info->tile_x_sa;
      *tile_y_px = info->tile_y_sa;
   }
}

static void
surf_convert_to_single_slice(const struct isl_device *isl_dev,
                             struct brw_blorp_surface_info *info)
{
   /* Just bail if we have nothing to do. */
   if (info->surf.dim == ISL_SURF_DIM_2D &&
       info->view.base_level == 0 && info->view.base_array_layer == 0 &&
       info->surf.levels == 1 && info->surf.logical_level0_px.array_len == 1)
      return;

   /* If this gets triggered then we've gotten here twice which.  This
    * shouldn't happen thanks to the above early return.
    */
   assert(info->tile_x_sa == 0 && info->tile_y_sa == 0);

   uint32_t layer = 0, z = 0;
   if (info->surf.dim == ISL_SURF_DIM_3D)
      z = info->view.base_array_layer + info->z_offset;
   else
      layer = info->view.base_array_layer;

   uint32_t x_offset_sa, y_offset_sa;
   isl_surf_get_image_offset_sa(&info->surf, info->view.base_level,
                                layer, z, &x_offset_sa, &y_offset_sa);

   uint32_t byte_offset;
   isl_tiling_get_intratile_offset_sa(isl_dev, info->surf.tiling,
                                      info->surf.format, info->surf.row_pitch,
                                      x_offset_sa, y_offset_sa,
                                      &byte_offset,
                                      &info->tile_x_sa, &info->tile_y_sa);
   info->addr.offset += byte_offset;

   const uint32_t slice_width_px =
      minify(info->surf.logical_level0_px.width, info->view.base_level);
   const uint32_t slice_height_px =
      minify(info->surf.logical_level0_px.height, info->view.base_level);

   uint32_t tile_x_px, tile_y_px;
   surf_get_intratile_offset_px(info, &tile_x_px, &tile_y_px);

   struct isl_surf_init_info init_info = {
      .dim = ISL_SURF_DIM_2D,
      .format = info->surf.format,
      .width = slice_width_px + tile_x_px,
      .height = slice_height_px + tile_y_px,
      .depth = 1,
      .levels = 1,
      .array_len = 1,
      .samples = info->surf.samples,
      .min_pitch = info->surf.row_pitch,
      .usage = info->surf.usage,
      .tiling_flags = 1 << info->surf.tiling,
   };

   isl_surf_init_s(isl_dev, &info->surf, &init_info);
   assert(info->surf.row_pitch == init_info.min_pitch);

   /* The view is also different now. */
   info->view.base_level = 0;
   info->view.levels = 1;
   info->view.base_array_layer = 0;
   info->view.array_len = 1;
   info->z_offset = 0;
}

static void
surf_fake_interleaved_msaa(const struct isl_device *isl_dev,
                           struct brw_blorp_surface_info *info)
{
   assert(info->surf.msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED);

   /* First, we need to convert it to a simple 1-level 1-layer 2-D surface */
   surf_convert_to_single_slice(isl_dev, info);

   info->surf.logical_level0_px = info->surf.phys_level0_sa;
   info->surf.samples = 1;
   info->surf.msaa_layout = ISL_MSAA_LAYOUT_NONE;
}

static void
surf_retile_w_to_y(const struct isl_device *isl_dev,
                   struct brw_blorp_surface_info *info)
{
   assert(info->surf.tiling == ISL_TILING_W);

   /* First, we need to convert it to a simple 1-level 1-layer 2-D surface */
   surf_convert_to_single_slice(isl_dev, info);

   /* On gen7+, we don't have interleaved multisampling for color render
    * targets so we have to fake it.
    *
    * TODO: Are we sure we don't also need to fake it on gen6?
    */
   if (isl_dev->info->gen > 6 &&
       info->surf.msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED) {
      surf_fake_interleaved_msaa(isl_dev, info);
   }

   if (isl_dev->info->gen == 6) {
      /* Gen6 stencil buffers have a very large alignment coming in from the
       * miptree.  It's out-of-bounds for what the surface state can handle.
       * Since we have a single layer and level, it doesn't really matter as
       * long as we don't pass a bogus value into isl_surf_fill_state().
       */
      info->surf.image_alignment_el = isl_extent3d(4, 2, 1);
   }

   /* Now that we've converted everything to a simple 2-D surface with only
    * one miplevel, we can go about retiling it.
    */
   const unsigned x_align = 8, y_align = info->surf.samples != 0 ? 8 : 4;
   info->surf.tiling = ISL_TILING_Y0;
   info->surf.logical_level0_px.width =
      ALIGN(info->surf.logical_level0_px.width, x_align) * 2;
   info->surf.logical_level0_px.height =
      ALIGN(info->surf.logical_level0_px.height, y_align) / 2;
   info->tile_x_sa *= 2;
   info->tile_y_sa /= 2;
}

static bool
can_shrink_surface(const struct brw_blorp_surface_info *surf)
{
   /* The current code doesn't support offsets into the aux buffers. This
    * should be possible, but we need to make sure the offset is page
    * aligned for both the surface and the aux buffer surface. Generally
    * this mean using the page aligned offset for the aux buffer.
    *
    * Currently the cases where we must split the blit are limited to cases
    * where we don't have a aux buffer.
    */
   if (surf->aux_addr.buffer != NULL)
      return false;

   /* We can't support splitting the blit for gen <= 7, because the qpitch
    * size is calculated by the hardware based on the surface height for
    * gen <= 7. In gen >= 8, the qpitch is controlled by the driver.
    */
   if (surf->surf.msaa_layout == ISL_MSAA_LAYOUT_ARRAY)
      return false;

   return true;
}

static bool
can_shrink_surfaces(const struct blorp_params *params)
{
   return
      can_shrink_surface(&params->src) &&
      can_shrink_surface(&params->dst);
}

static unsigned
get_max_surface_size(const struct gen_device_info *devinfo,
                     const struct blorp_params *params)
{
   const unsigned max = devinfo->gen >= 7 ? 16384 : 8192;
   if (split_blorp_blit_debug && can_shrink_surfaces(params))
      return max >> 4; /* A smaller restriction when debug is enabled */
   else
      return max;
}

struct blt_axis {
   double src0, src1, dst0, dst1;
   bool mirror;
};

struct blt_coords {
   struct blt_axis x, y;
};

static void
surf_fake_rgb_with_red(const struct isl_device *isl_dev,
                       struct brw_blorp_surface_info *info,
                       uint32_t *x, uint32_t *width)
{
   surf_convert_to_single_slice(isl_dev, info);

   info->surf.logical_level0_px.width *= 3;
   info->surf.phys_level0_sa.width *= 3;
   *x *= 3;
   *width *= 3;

   enum isl_format red_format;
   switch (info->view.format) {
   case ISL_FORMAT_R8G8B8_UNORM:
      red_format = ISL_FORMAT_R8_UNORM;
      break;
   case ISL_FORMAT_R8G8B8_UINT:
      red_format = ISL_FORMAT_R8_UINT;
      break;
   case ISL_FORMAT_R16G16B16_UNORM:
      red_format = ISL_FORMAT_R16_UNORM;
      break;
   case ISL_FORMAT_R16G16B16_UINT:
      red_format = ISL_FORMAT_R16_UINT;
      break;
   case ISL_FORMAT_R32G32B32_UINT:
      red_format = ISL_FORMAT_R32_UINT;
      break;
   default:
      unreachable("Invalid RGB copy destination format");
   }
   assert(isl_format_get_layout(red_format)->channels.r.type ==
          isl_format_get_layout(info->view.format)->channels.r.type);
   assert(isl_format_get_layout(red_format)->channels.r.bits ==
          isl_format_get_layout(info->view.format)->channels.r.bits);

   info->surf.format = info->view.format = red_format;
}

static void
fake_dest_rgb_with_red(const struct isl_device *dev,
                       struct blorp_params *params,
                       struct brw_blorp_blit_prog_key *wm_prog_key,
                       struct blt_coords *coords)
{
   /* Handle RGB destinations for blorp_copy */
   const struct isl_format_layout *dst_fmtl =
      isl_format_get_layout(params->dst.surf.format);

   if (dst_fmtl->bpb % 3 == 0) {
      uint32_t dst_x = coords->x.dst0;
      uint32_t dst_width = coords->x.dst1 - dst_x;
      surf_fake_rgb_with_red(dev, &params->dst,
                             &dst_x, &dst_width);
      coords->x.dst0 = dst_x;
      coords->x.dst1 = dst_x + dst_width;
      wm_prog_key->dst_rgb = true;
      wm_prog_key->need_dst_offset = true;
   }
}

enum blit_shrink_status {
   BLIT_NO_SHRINK = 0,
   BLIT_WIDTH_SHRINK = 1,
   BLIT_HEIGHT_SHRINK = 2,
};

/* Try to blit. If the surface parameters exceed the size allowed by hardware,
 * then enum blit_shrink_status will be returned. If BLIT_NO_SHRINK is
 * returned, then the blit was successful.
 */
static enum blit_shrink_status
try_blorp_blit(struct blorp_batch *batch,
               struct blorp_params *params,
               struct brw_blorp_blit_prog_key *wm_prog_key,
               struct blt_coords *coords)
{
   const struct gen_device_info *devinfo = batch->blorp->isl_dev->info;

   fake_dest_rgb_with_red(batch->blorp->isl_dev, params, wm_prog_key, coords);

   if (isl_format_has_sint_channel(params->src.view.format)) {
      wm_prog_key->texture_data_type = nir_type_int;
   } else if (isl_format_has_uint_channel(params->src.view.format)) {
      wm_prog_key->texture_data_type = nir_type_uint;
   } else {
      wm_prog_key->texture_data_type = nir_type_float;
   }

   /* src_samples and dst_samples are the true sample counts */
   wm_prog_key->src_samples = params->src.surf.samples;
   wm_prog_key->dst_samples = params->dst.surf.samples;

   wm_prog_key->tex_aux_usage = params->src.aux_usage;

   /* src_layout and dst_layout indicate the true MSAA layout used by src and
    * dst.
    */
   wm_prog_key->src_layout = params->src.surf.msaa_layout;
   wm_prog_key->dst_layout = params->dst.surf.msaa_layout;

   /* Round floating point values to nearest integer to avoid "off by one texel"
    * kind of errors when blitting.
    */
   params->x0 = params->wm_inputs.discard_rect.x0 = round(coords->x.dst0);
   params->y0 = params->wm_inputs.discard_rect.y0 = round(coords->y.dst0);
   params->x1 = params->wm_inputs.discard_rect.x1 = round(coords->x.dst1);
   params->y1 = params->wm_inputs.discard_rect.y1 = round(coords->y.dst1);

   brw_blorp_setup_coord_transform(&params->wm_inputs.coord_transform[0],
                                   coords->x.src0, coords->x.src1,
                                   coords->x.dst0, coords->x.dst1,
                                   coords->x.mirror);
   brw_blorp_setup_coord_transform(&params->wm_inputs.coord_transform[1],
                                   coords->y.src0, coords->y.src1,
                                   coords->y.dst0, coords->y.dst1,
                                   coords->y.mirror);

   if (devinfo->gen > 6 &&
       params->dst.surf.msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED) {
      assert(params->dst.surf.samples > 1);

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
      struct isl_extent2d px_size_sa =
         isl_get_interleaved_msaa_px_size_sa(params->dst.surf.samples);
      params->x0 = ROUND_DOWN_TO(params->x0, 2) * px_size_sa.width;
      params->y0 = ROUND_DOWN_TO(params->y0, 2) * px_size_sa.height;
      params->x1 = ALIGN(params->x1, 2) * px_size_sa.width;
      params->y1 = ALIGN(params->y1, 2) * px_size_sa.height;

      surf_fake_interleaved_msaa(batch->blorp->isl_dev, &params->dst);

      wm_prog_key->use_kill = true;
      wm_prog_key->need_dst_offset = true;
   }

   if (params->dst.surf.tiling == ISL_TILING_W) {
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
      const unsigned x_align = 8;
      const unsigned y_align = params->dst.surf.samples != 0 ? 8 : 4;
      params->x0 = ROUND_DOWN_TO(params->x0, x_align) * 2;
      params->y0 = ROUND_DOWN_TO(params->y0, y_align) / 2;
      params->x1 = ALIGN(params->x1, x_align) * 2;
      params->y1 = ALIGN(params->y1, y_align) / 2;

      /* Retile the surface to Y-tiled */
      surf_retile_w_to_y(batch->blorp->isl_dev, &params->dst);

      wm_prog_key->dst_tiled_w = true;
      wm_prog_key->use_kill = true;
      wm_prog_key->need_dst_offset = true;

      if (params->dst.surf.samples > 1) {
         /* If the destination surface is a W-tiled multisampled stencil
          * buffer that we're mapping as Y tiled, then we need to arrange for
          * the WM program to run once per sample rather than once per pixel,
          * because the memory layout of related samples doesn't match between
          * W and Y tiling.
          */
         wm_prog_key->persample_msaa_dispatch = true;
      }
   }

   if (devinfo->gen < 8 && params->src.surf.tiling == ISL_TILING_W) {
      /* On Haswell and earlier, we have to fake W-tiled sources as Y-tiled.
       * Broadwell adds support for sampling from stencil.
       *
       * See the comments above concerning x/y offset alignment for the
       * destination surface.
       *
       * TODO: what if this makes the texture size too large?
       */
      surf_retile_w_to_y(batch->blorp->isl_dev, &params->src);

      wm_prog_key->src_tiled_w = true;
      wm_prog_key->need_src_offset = true;
   }

   /* tex_samples and rt_samples are the sample counts that are set up in
    * SURFACE_STATE.
    */
   wm_prog_key->tex_samples = params->src.surf.samples;
   wm_prog_key->rt_samples  = params->dst.surf.samples;

   /* tex_layout and rt_layout indicate the MSAA layout the GPU pipeline will
    * use to access the source and destination surfaces.
    */
   wm_prog_key->tex_layout = params->src.surf.msaa_layout;
   wm_prog_key->rt_layout = params->dst.surf.msaa_layout;

   if (params->src.surf.samples > 0 && params->dst.surf.samples > 1) {
      /* We are blitting from a multisample buffer to a multisample buffer, so
       * we must preserve samples within a pixel.  This means we have to
       * arrange for the WM program to run once per sample rather than once
       * per pixel.
       */
      wm_prog_key->persample_msaa_dispatch = true;
   }

   params->num_samples = params->dst.surf.samples;

   if (params->src.tile_x_sa || params->src.tile_y_sa) {
      assert(wm_prog_key->need_src_offset);
      surf_get_intratile_offset_px(&params->src,
                                   &params->wm_inputs.src_offset.x,
                                   &params->wm_inputs.src_offset.y);
   }

   if (params->dst.tile_x_sa || params->dst.tile_y_sa) {
      assert(wm_prog_key->need_dst_offset);
      surf_get_intratile_offset_px(&params->dst,
                                   &params->wm_inputs.dst_offset.x,
                                   &params->wm_inputs.dst_offset.y);
      params->x0 += params->wm_inputs.dst_offset.x;
      params->y0 += params->wm_inputs.dst_offset.y;
      params->x1 += params->wm_inputs.dst_offset.x;
      params->y1 += params->wm_inputs.dst_offset.y;
   }

   /* For some texture types, we need to pass the layer through the sampler. */
   params->wm_inputs.src_z = params->src.z_offset;

   brw_blorp_get_blit_kernel(batch->blorp, params, wm_prog_key);

   unsigned result = 0;
   unsigned max_surface_size = get_max_surface_size(devinfo, params);
   if (params->src.surf.logical_level0_px.width > max_surface_size ||
       params->dst.surf.logical_level0_px.width > max_surface_size)
      result |= BLIT_WIDTH_SHRINK;
   if (params->src.surf.logical_level0_px.height > max_surface_size ||
       params->dst.surf.logical_level0_px.height > max_surface_size)
      result |= BLIT_HEIGHT_SHRINK;

   if (result == 0) {
      batch->blorp->exec(batch, params);
   }

   return result;
}

/* Adjust split blit source coordinates for the current destination
 * coordinates.
 */
static void
adjust_split_source_coords(const struct blt_axis *orig,
                           struct blt_axis *split_coords,
                           double scale)
{
   /* When scale is greater than 0, then we are growing from the start, so
    * src0 uses delta0, and src1 uses delta1. When scale is less than 0, the
    * source range shrinks from the end. In that case src0 is adjusted by
    * delta1, and src1 is adjusted by delta0.
    */
   double delta0 = scale * (split_coords->dst0 - orig->dst0);
   double delta1 = scale * (split_coords->dst1 - orig->dst1);
   split_coords->src0 = orig->src0 + (scale >= 0.0 ? delta0 : delta1);
   split_coords->src1 = orig->src1 + (scale >= 0.0 ? delta1 : delta0);
}

static const struct isl_extent2d
get_px_size_sa(const struct isl_surf *surf)
{
   static const struct isl_extent2d one_to_one = { .w = 1, .h = 1 };

   if (surf->msaa_layout != ISL_MSAA_LAYOUT_INTERLEAVED)
      return one_to_one;
   else
      return isl_get_interleaved_msaa_px_size_sa(surf->samples);
}

static void
shrink_surface_params(const struct isl_device *dev,
                      struct brw_blorp_surface_info *info,
                      double *x0, double *x1, double *y0, double *y1)
{
   uint32_t byte_offset, x_offset_sa, y_offset_sa, size;
   struct isl_extent2d px_size_sa;
   int adjust;

   surf_convert_to_single_slice(dev, info);

   px_size_sa = get_px_size_sa(&info->surf);

   /* Because this gets called after we lower compressed images, the tile
    * offsets may be non-zero and we need to incorporate them in our
    * calculations.
    */
   x_offset_sa = (uint32_t)*x0 * px_size_sa.w + info->tile_x_sa;
   y_offset_sa = (uint32_t)*y0 * px_size_sa.h + info->tile_y_sa;
   isl_tiling_get_intratile_offset_sa(dev, info->surf.tiling,
                                      info->surf.format, info->surf.row_pitch,
                                      x_offset_sa, y_offset_sa,
                                      &byte_offset,
                                      &info->tile_x_sa, &info->tile_y_sa);

   info->addr.offset += byte_offset;

   adjust = (int)info->tile_x_sa / px_size_sa.w - (int)*x0;
   *x0 += adjust;
   *x1 += adjust;
   info->tile_x_sa = 0;

   adjust = (int)info->tile_y_sa / px_size_sa.h - (int)*y0;
   *y0 += adjust;
   *y1 += adjust;
   info->tile_y_sa = 0;

   size = MIN2((uint32_t)ceil(*x1), info->surf.logical_level0_px.width);
   info->surf.logical_level0_px.width = size;
   info->surf.phys_level0_sa.width = size * px_size_sa.w;

   size = MIN2((uint32_t)ceil(*y1), info->surf.logical_level0_px.height);
   info->surf.logical_level0_px.height = size;
   info->surf.phys_level0_sa.height = size * px_size_sa.h;
}

static void
shrink_surfaces(const struct isl_device *dev,
                struct blorp_params *params,
                struct brw_blorp_blit_prog_key *wm_prog_key,
                struct blt_coords *coords)
{
   /* Shrink source surface */
   shrink_surface_params(dev, &params->src, &coords->x.src0, &coords->x.src1,
                         &coords->y.src0, &coords->y.src1);
   wm_prog_key->need_src_offset = false;

   /* Shrink destination surface */
   shrink_surface_params(dev, &params->dst, &coords->x.dst0, &coords->x.dst1,
                         &coords->y.dst0, &coords->y.dst1);
   wm_prog_key->need_dst_offset = false;
}

static void
do_blorp_blit(struct blorp_batch *batch,
              const struct blorp_params *orig_params,
              struct brw_blorp_blit_prog_key *wm_prog_key,
              const struct blt_coords *orig)
{
   struct blorp_params params;
   struct blt_coords blit_coords;
   struct blt_coords split_coords = *orig;
   double w = orig->x.dst1 - orig->x.dst0;
   double h = orig->y.dst1 - orig->y.dst0;
   double x_scale = (orig->x.src1 - orig->x.src0) / w;
   double y_scale = (orig->y.src1 - orig->y.src0) / h;
   if (orig->x.mirror)
      x_scale = -x_scale;
   if (orig->y.mirror)
      y_scale = -y_scale;

   bool x_done, y_done;
   bool shrink = split_blorp_blit_debug && can_shrink_surfaces(orig_params);
   do {
      params = *orig_params;
      blit_coords = split_coords;
      if (shrink)
         shrink_surfaces(batch->blorp->isl_dev, &params, wm_prog_key,
                         &blit_coords);
      enum blit_shrink_status result =
         try_blorp_blit(batch, &params, wm_prog_key, &blit_coords);

      if (result & BLIT_WIDTH_SHRINK) {
         w /= 2.0;
         assert(w >= 1.0);
         split_coords.x.dst1 = MIN2(split_coords.x.dst0 + w, orig->x.dst1);
         adjust_split_source_coords(&orig->x, &split_coords.x, x_scale);
      }
      if (result & BLIT_HEIGHT_SHRINK) {
         h /= 2.0;
         assert(h >= 1.0);
         split_coords.y.dst1 = MIN2(split_coords.y.dst0 + h, orig->y.dst1);
         adjust_split_source_coords(&orig->y, &split_coords.y, y_scale);
      }

      if (result != 0) {
         assert(can_shrink_surfaces(orig_params));
         shrink = true;
         continue;
      }

      y_done = (orig->y.dst1 - split_coords.y.dst1 < 0.5);
      x_done = y_done && (orig->x.dst1 - split_coords.x.dst1 < 0.5);
      if (x_done) {
         break;
      } else if (y_done) {
         split_coords.x.dst0 += w;
         split_coords.x.dst1 = MIN2(split_coords.x.dst0 + w, orig->x.dst1);
         split_coords.y.dst0 = orig->y.dst0;
         split_coords.y.dst1 = MIN2(split_coords.y.dst0 + h, orig->y.dst1);
         adjust_split_source_coords(&orig->x, &split_coords.x, x_scale);
      } else {
         split_coords.y.dst0 += h;
         split_coords.y.dst1 = MIN2(split_coords.y.dst0 + h, orig->y.dst1);
         adjust_split_source_coords(&orig->y, &split_coords.y, y_scale);
      }
   } while (true);
}

void
blorp_blit(struct blorp_batch *batch,
           const struct blorp_surf *src_surf,
           unsigned src_level, unsigned src_layer,
           enum isl_format src_format, struct isl_swizzle src_swizzle,
           const struct blorp_surf *dst_surf,
           unsigned dst_level, unsigned dst_layer,
           enum isl_format dst_format, struct isl_swizzle dst_swizzle,
           float src_x0, float src_y0,
           float src_x1, float src_y1,
           float dst_x0, float dst_y0,
           float dst_x1, float dst_y1,
           GLenum filter, bool mirror_x, bool mirror_y)
{
   struct blorp_params params;
   blorp_params_init(&params);

   brw_blorp_surface_info_init(batch->blorp, &params.src, src_surf, src_level,
                               src_layer, src_format, false);
   brw_blorp_surface_info_init(batch->blorp, &params.dst, dst_surf, dst_level,
                               dst_layer, dst_format, true);

   params.src.view.swizzle = src_swizzle;
   params.dst.view.swizzle = dst_swizzle;

   struct brw_blorp_blit_prog_key wm_prog_key = {
      .shader_type = BLORP_SHADER_TYPE_BLIT
   };

   /* Scaled blitting or not. */
   wm_prog_key.blit_scaled =
      ((dst_x1 - dst_x0) == (src_x1 - src_x0) &&
       (dst_y1 - dst_y0) == (src_y1 - src_y0)) ? false : true;

   /* Scaling factors used for bilinear filtering in multisample scaled
    * blits.
    */
   if (params.src.surf.samples == 16)
      wm_prog_key.x_scale = 4.0f;
   else
      wm_prog_key.x_scale = 2.0f;
   wm_prog_key.y_scale = params.src.surf.samples / wm_prog_key.x_scale;

   if (filter == GL_LINEAR &&
       params.src.surf.samples <= 1 && params.dst.surf.samples <= 1)
      wm_prog_key.bilinear_filter = true;

   if ((params.src.surf.usage & ISL_SURF_USAGE_DEPTH_BIT) == 0 &&
       (params.src.surf.usage & ISL_SURF_USAGE_STENCIL_BIT) == 0 &&
       !isl_format_has_int_channel(params.src.surf.format) &&
       params.src.surf.samples > 1 && params.dst.surf.samples <= 1) {
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

   params.wm_inputs.rect_grid.x1 =
      minify(params.src.surf.logical_level0_px.width, src_level) *
      wm_prog_key.x_scale - 1.0f;
   params.wm_inputs.rect_grid.y1 =
      minify(params.src.surf.logical_level0_px.height, src_level) *
      wm_prog_key.y_scale - 1.0f;

   struct blt_coords coords = {
      .x = {
         .src0 = src_x0,
         .src1 = src_x1,
         .dst0 = dst_x0,
         .dst1 = dst_x1,
         .mirror = mirror_x
      },
      .y = {
         .src0 = src_y0,
         .src1 = src_y1,
         .dst0 = dst_y0,
         .dst1 = dst_y1,
         .mirror = mirror_y
      }
   };

   do_blorp_blit(batch, &params, &wm_prog_key, &coords);
}

static enum isl_format
get_copy_format_for_bpb(const struct isl_device *isl_dev, unsigned bpb)
{
   /* The choice of UNORM and UINT formats is very intentional here.  Most
    * of the time, we want to use a UINT format to avoid any rounding error
    * in the blit.  For stencil blits, R8_UINT is required by the hardware.
    * (It's the only format allowed in conjunction with W-tiling.)  Also we
    * intentionally use the 4-channel formats whenever we can.  This is so
    * that, when we do a RGB <-> RGBX copy, the two formats will line up
    * even though one of them is 3/4 the size of the other.  The choice of
    * UNORM vs. UINT is also very intentional because we don't have 8 or
    * 16-bit RGB UINT formats until Sky Lake so we have to use UNORM there.
    * Fortunately, the only time we should ever use two different formats in
    * the table below is for RGB -> RGBA blits and so we will never have any
    * UNORM/UINT mismatch.
    */
   if (ISL_DEV_GEN(isl_dev) >= 9) {
      switch (bpb) {
      case 8:  return ISL_FORMAT_R8_UINT;
      case 16: return ISL_FORMAT_R8G8_UINT;
      case 24: return ISL_FORMAT_R8G8B8_UINT;
      case 32: return ISL_FORMAT_R8G8B8A8_UINT;
      case 48: return ISL_FORMAT_R16G16B16_UINT;
      case 64: return ISL_FORMAT_R16G16B16A16_UINT;
      case 96: return ISL_FORMAT_R32G32B32_UINT;
      case 128:return ISL_FORMAT_R32G32B32A32_UINT;
      default:
         unreachable("Unknown format bpb");
      }
   } else {
      switch (bpb) {
      case 8:  return ISL_FORMAT_R8_UINT;
      case 16: return ISL_FORMAT_R8G8_UINT;
      case 24: return ISL_FORMAT_R8G8B8_UNORM;
      case 32: return ISL_FORMAT_R8G8B8A8_UNORM;
      case 48: return ISL_FORMAT_R16G16B16_UNORM;
      case 64: return ISL_FORMAT_R16G16B16A16_UNORM;
      case 96: return ISL_FORMAT_R32G32B32_UINT;
      case 128:return ISL_FORMAT_R32G32B32A32_UINT;
      default:
         unreachable("Unknown format bpb");
      }
   }
}

/** Returns a UINT format that is CCS-compatible with the given format
 *
 * The PRM's say absolutely nothing about how render compression works.  The
 * only thing they provide is a list of formats on which it is and is not
 * supported.  Empirical testing indicates that the compression is only based
 * on the bit-layout of the format and the channel encoding doesn't matter.
 * So, while texture views don't work in general, you can create a view as
 * long as the bit-layout of the formats are the same.
 *
 * Fortunately, for every render compression capable format, the UINT format
 * with the same bit layout also supports render compression.  This means that
 * we only need to handle UINT formats for copy operations.  In order to do
 * copies between formats with different bit layouts, we attach both with a
 * UINT format and use bit_cast_color() to generate code to do the bit-cast
 * operation between the two bit layouts.
 */
static enum isl_format
get_ccs_compatible_uint_format(const struct isl_format_layout *fmtl)
{
   switch (fmtl->format) {
   case ISL_FORMAT_R32G32B32A32_FLOAT:
   case ISL_FORMAT_R32G32B32A32_SINT:
   case ISL_FORMAT_R32G32B32A32_UINT:
   case ISL_FORMAT_R32G32B32A32_UNORM:
   case ISL_FORMAT_R32G32B32A32_SNORM:
      return ISL_FORMAT_R32G32B32A32_UINT;

   case ISL_FORMAT_R16G16B16A16_UNORM:
   case ISL_FORMAT_R16G16B16A16_SNORM:
   case ISL_FORMAT_R16G16B16A16_SINT:
   case ISL_FORMAT_R16G16B16A16_UINT:
   case ISL_FORMAT_R16G16B16A16_FLOAT:
   case ISL_FORMAT_R16G16B16X16_UNORM:
   case ISL_FORMAT_R16G16B16X16_FLOAT:
      return ISL_FORMAT_R16G16B16A16_UINT;

   case ISL_FORMAT_R32G32_FLOAT:
   case ISL_FORMAT_R32G32_SINT:
   case ISL_FORMAT_R32G32_UINT:
   case ISL_FORMAT_R32G32_UNORM:
   case ISL_FORMAT_R32G32_SNORM:
      return ISL_FORMAT_R32G32_UINT;

   case ISL_FORMAT_B8G8R8A8_UNORM:
   case ISL_FORMAT_B8G8R8A8_UNORM_SRGB:
   case ISL_FORMAT_R8G8B8A8_UNORM:
   case ISL_FORMAT_R8G8B8A8_UNORM_SRGB:
   case ISL_FORMAT_R8G8B8A8_SNORM:
   case ISL_FORMAT_R8G8B8A8_SINT:
   case ISL_FORMAT_R8G8B8A8_UINT:
   case ISL_FORMAT_B8G8R8X8_UNORM:
   case ISL_FORMAT_B8G8R8X8_UNORM_SRGB:
   case ISL_FORMAT_R8G8B8X8_UNORM:
   case ISL_FORMAT_R8G8B8X8_UNORM_SRGB:
      return ISL_FORMAT_R8G8B8A8_UINT;

   case ISL_FORMAT_R16G16_UNORM:
   case ISL_FORMAT_R16G16_SNORM:
   case ISL_FORMAT_R16G16_SINT:
   case ISL_FORMAT_R16G16_UINT:
   case ISL_FORMAT_R16G16_FLOAT:
      return ISL_FORMAT_R16G16_UINT;

   case ISL_FORMAT_R32_SINT:
   case ISL_FORMAT_R32_UINT:
   case ISL_FORMAT_R32_FLOAT:
   case ISL_FORMAT_R32_UNORM:
   case ISL_FORMAT_R32_SNORM:
      return ISL_FORMAT_R32_UINT;

   default:
      unreachable("Not a compressible format");
   }
}

/* Takes an isl_color_value and returns a color value that is the original
 * color value only bit-casted to a UINT format.  This value, together with
 * the format from get_ccs_compatible_uint_format, will yield the same bit
 * value as the original color and format.
 */
static union isl_color_value
bitcast_color_value_to_uint(union isl_color_value color,
                            const struct isl_format_layout *fmtl)
{
   /* All CCS formats have the same number of bits in each channel */
   const struct isl_channel_layout *chan = &fmtl->channels.r;

   union isl_color_value bits;
   switch (chan->type) {
   case ISL_UINT:
   case ISL_SINT:
      /* Hardware will ignore the high bits so there's no need to cast */
      bits = color;
      break;

   case ISL_UNORM:
      for (unsigned i = 0; i < 4; i++)
         bits.u32[i] = _mesa_float_to_unorm(color.f32[i], chan->bits);
      break;

   case ISL_SNORM:
      for (unsigned i = 0; i < 4; i++)
         bits.i32[i] = _mesa_float_to_snorm(color.f32[i], chan->bits);
      break;

   case ISL_SFLOAT:
      switch (chan->bits) {
      case 16:
         for (unsigned i = 0; i < 4; i++)
            bits.u32[i] = _mesa_float_to_half(color.f32[i]);
         break;

      case 32:
         bits = color;
         break;

      default:
         unreachable("Invalid float format size");
      }
      break;

   default:
      unreachable("Invalid channel type");
   }

   switch (fmtl->format) {
   case ISL_FORMAT_B8G8R8A8_UNORM:
   case ISL_FORMAT_B8G8R8A8_UNORM_SRGB:
   case ISL_FORMAT_B8G8R8X8_UNORM:
   case ISL_FORMAT_B8G8R8X8_UNORM_SRGB: {
      /* If it's a BGRA format, we need to swap blue and red */
      uint32_t tmp = bits.u32[0];
      bits.u32[0] = bits.u32[2];
      bits.u32[2] = tmp;
      break;
   }

   default:
      break; /* Nothing to do */
   }

   return bits;
}

static void
surf_convert_to_uncompressed(const struct isl_device *isl_dev,
                             struct brw_blorp_surface_info *info,
                             uint32_t *x, uint32_t *y,
                             uint32_t *width, uint32_t *height)
{
   const struct isl_format_layout *fmtl =
      isl_format_get_layout(info->surf.format);

   assert(fmtl->bw > 1 || fmtl->bh > 1);

   /* This is a compressed surface.  We need to convert it to a single
    * slice (because compressed layouts don't perfectly match uncompressed
    * ones with the same bpb) and divide x, y, width, and height by the
    * block size.
    */
   surf_convert_to_single_slice(isl_dev, info);

   if (width || height) {
#ifndef NDEBUG
      uint32_t right_edge_px = info->tile_x_sa + *x + *width;
      uint32_t bottom_edge_px = info->tile_y_sa + *y + *height;
      assert(*width % fmtl->bw == 0 ||
             right_edge_px == info->surf.logical_level0_px.width);
      assert(*height % fmtl->bh == 0 ||
             bottom_edge_px == info->surf.logical_level0_px.height);
#endif
      *width = DIV_ROUND_UP(*width, fmtl->bw);
      *height = DIV_ROUND_UP(*height, fmtl->bh);
   }

   assert(*x % fmtl->bw == 0);
   assert(*y % fmtl->bh == 0);
   *x /= fmtl->bw;
   *y /= fmtl->bh;

   info->surf.logical_level0_px.width =
      DIV_ROUND_UP(info->surf.logical_level0_px.width, fmtl->bw);
   info->surf.logical_level0_px.height =
      DIV_ROUND_UP(info->surf.logical_level0_px.height, fmtl->bh);

   assert(info->surf.phys_level0_sa.width % fmtl->bw == 0);
   assert(info->surf.phys_level0_sa.height % fmtl->bh == 0);
   info->surf.phys_level0_sa.width /= fmtl->bw;
   info->surf.phys_level0_sa.height /= fmtl->bh;

   assert(info->tile_x_sa % fmtl->bw == 0);
   assert(info->tile_y_sa % fmtl->bh == 0);
   info->tile_x_sa /= fmtl->bw;
   info->tile_y_sa /= fmtl->bh;

   /* It's now an uncompressed surface so we need an uncompressed format */
   info->surf.format = get_copy_format_for_bpb(isl_dev, fmtl->bpb);
}

void
blorp_copy(struct blorp_batch *batch,
           const struct blorp_surf *src_surf,
           unsigned src_level, unsigned src_layer,
           const struct blorp_surf *dst_surf,
           unsigned dst_level, unsigned dst_layer,
           uint32_t src_x, uint32_t src_y,
           uint32_t dst_x, uint32_t dst_y,
           uint32_t src_width, uint32_t src_height)
{
   const struct isl_device *isl_dev = batch->blorp->isl_dev;
   struct blorp_params params;

   if (src_width == 0 || src_height == 0)
      return;

   blorp_params_init(&params);
   brw_blorp_surface_info_init(batch->blorp, &params.src, src_surf, src_level,
                               src_layer, ISL_FORMAT_UNSUPPORTED, false);
   brw_blorp_surface_info_init(batch->blorp, &params.dst, dst_surf, dst_level,
                               dst_layer, ISL_FORMAT_UNSUPPORTED, true);

   struct brw_blorp_blit_prog_key wm_prog_key = {
      .shader_type = BLORP_SHADER_TYPE_BLIT
   };

   const struct isl_format_layout *src_fmtl =
      isl_format_get_layout(params.src.surf.format);
   const struct isl_format_layout *dst_fmtl =
      isl_format_get_layout(params.dst.surf.format);

   assert(params.src.aux_usage == ISL_AUX_USAGE_NONE ||
          params.src.aux_usage == ISL_AUX_USAGE_MCS ||
          params.src.aux_usage == ISL_AUX_USAGE_CCS_E);
   assert(params.dst.aux_usage == ISL_AUX_USAGE_NONE ||
          params.dst.aux_usage == ISL_AUX_USAGE_MCS ||
          params.dst.aux_usage == ISL_AUX_USAGE_CCS_E);

   if (params.dst.aux_usage == ISL_AUX_USAGE_CCS_E) {
      params.dst.view.format = get_ccs_compatible_uint_format(dst_fmtl);
      if (params.src.aux_usage == ISL_AUX_USAGE_CCS_E) {
         params.src.view.format = get_ccs_compatible_uint_format(src_fmtl);
      } else if (src_fmtl->bpb == dst_fmtl->bpb) {
         params.src.view.format = params.dst.view.format;
      } else {
         params.src.view.format =
            get_copy_format_for_bpb(isl_dev, src_fmtl->bpb);
      }
   } else if (params.src.aux_usage == ISL_AUX_USAGE_CCS_E) {
      params.src.view.format = get_ccs_compatible_uint_format(src_fmtl);
      if (src_fmtl->bpb == dst_fmtl->bpb) {
         params.dst.view.format = params.src.view.format;
      } else {
         params.dst.view.format =
            get_copy_format_for_bpb(isl_dev, dst_fmtl->bpb);
      }
   } else {
      params.dst.view.format = get_copy_format_for_bpb(isl_dev, dst_fmtl->bpb);
      params.src.view.format = get_copy_format_for_bpb(isl_dev, src_fmtl->bpb);
   }

   if (params.src.aux_usage == ISL_AUX_USAGE_CCS_E) {
      params.src.clear_color =
         bitcast_color_value_to_uint(params.src.clear_color, src_fmtl);
   }

   if (params.dst.aux_usage == ISL_AUX_USAGE_CCS_E) {
      params.dst.clear_color =
         bitcast_color_value_to_uint(params.dst.clear_color, dst_fmtl);
   }

   wm_prog_key.src_bpc =
      isl_format_get_layout(params.src.view.format)->channels.r.bits;
   wm_prog_key.dst_bpc =
      isl_format_get_layout(params.dst.view.format)->channels.r.bits;

   if (src_fmtl->bw > 1 || src_fmtl->bh > 1) {
      surf_convert_to_uncompressed(batch->blorp->isl_dev, &params.src,
                                   &src_x, &src_y, &src_width, &src_height);
      wm_prog_key.need_src_offset = true;
   }

   if (dst_fmtl->bw > 1 || dst_fmtl->bh > 1) {
      surf_convert_to_uncompressed(batch->blorp->isl_dev, &params.dst,
                                   &dst_x, &dst_y, NULL, NULL);
      wm_prog_key.need_dst_offset = true;
   }

   /* Once both surfaces are stompped to uncompressed as needed, the
    * destination size is the same as the source size.
    */
   uint32_t dst_width = src_width;
   uint32_t dst_height = src_height;

   struct blt_coords coords = {
      .x = {
         .src0 = src_x,
         .src1 = src_x + src_width,
         .dst0 = dst_x,
         .dst1 = dst_x + dst_width,
         .mirror = false
      },
      .y = {
         .src0 = src_y,
         .src1 = src_y + src_height,
         .dst0 = dst_y,
         .dst1 = dst_y + dst_height,
         .mirror = false
      }
   };

   do_blorp_blit(batch, &params, &wm_prog_key, &coords);
}
