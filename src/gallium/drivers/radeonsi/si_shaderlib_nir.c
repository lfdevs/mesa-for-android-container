/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define AC_SURFACE_INCLUDE_NIR
#include "ac_surface.h"
#include "si_pipe.h"

static void *create_shader_state(struct si_context *sctx, nir_shader *nir)
{
   sctx->b.screen->finalize_nir(sctx->b.screen, (void*)nir);

   struct pipe_shader_state state = {0};
   state.type = PIPE_SHADER_IR_NIR;
   state.ir.nir = nir;

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
      return sctx->b.create_vs_state(&sctx->b, &state);
   case MESA_SHADER_TESS_CTRL:
      return sctx->b.create_tcs_state(&sctx->b, &state);
   case MESA_SHADER_TESS_EVAL:
      return sctx->b.create_tes_state(&sctx->b, &state);
   case MESA_SHADER_FRAGMENT:
      return sctx->b.create_fs_state(&sctx->b, &state);
   case MESA_SHADER_COMPUTE: {
      struct pipe_compute_state cs_state = {0};
      cs_state.ir_type = PIPE_SHADER_IR_NIR;
      cs_state.prog = nir;
      return sctx->b.create_compute_state(&sctx->b, &cs_state);
   }
   default:
      unreachable("invalid shader stage");
      return NULL;
   }
}

static nir_ssa_def *get_global_ids(nir_builder *b, unsigned num_components)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_ssa_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_ssa_def *block_ids = nir_channels(b, nir_load_workgroup_id(b, 32), mask);
   nir_ssa_def *block_size = nir_channels(b, nir_load_workgroup_size(b), mask);
   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

static void unpack_2x16(nir_builder *b, nir_ssa_def *src, nir_ssa_def **x, nir_ssa_def **y)
{
   *x = nir_iand(b, src, nir_imm_int(b, 0xffff));
   *y = nir_ushr(b, src, nir_imm_int(b, 16));
}

static nir_ssa_def *
deref_ssa(nir_builder *b, nir_variable *var)
{
   return &nir_build_deref_var(b, var)->dest.ssa;
}

/* Create a NIR compute shader implementing copy_image.
 *
 * This shader can handle 1D and 2D, linear and non-linear images.
 * It expects the source and destination (x,y,z) coords as user_data_amd,
 * packed into 3 SGPRs as 2x16bits per component.
 */
void *si_create_copy_image_cs(struct si_context *sctx, bool src_is_1d_array, bool dst_is_1d_array)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "copy_image_cs");
   b.shader->info.num_images = 2;

   /* The workgroup size is either 8x8 for normal (non-linear) 2D images,
    * or 64x1 for 1D and linear-2D images.
    */
   b.shader->info.workgroup_size_variable = true;

   b.shader->info.cs.user_data_components_amd = 3;
   nir_ssa_def *ids = get_global_ids(&b, 3);

   nir_ssa_def *coord_src = NULL, *coord_dst = NULL;
   unpack_2x16(&b, nir_load_user_data_amd(&b), &coord_src, &coord_dst);

   coord_src = nir_iadd(&b, coord_src, ids);
   coord_dst = nir_iadd(&b, coord_dst, ids);

   static unsigned swizzle_xz[] = {0, 2, 0, 0};

   if (src_is_1d_array)
      coord_src = nir_swizzle(&b, coord_src, swizzle_xz, 4);
   if (dst_is_1d_array)
      coord_dst = nir_swizzle(&b, coord_dst, swizzle_xz, 4);

   const struct glsl_type *src_img_type = glsl_image_type(src_is_1d_array ? GLSL_SAMPLER_DIM_1D
                                                                          : GLSL_SAMPLER_DIM_2D,
                                                          /*is_array*/ true, GLSL_TYPE_FLOAT);
   const struct glsl_type *dst_img_type = glsl_image_type(dst_is_1d_array ? GLSL_SAMPLER_DIM_1D
                                                                          : GLSL_SAMPLER_DIM_2D,
                                                          /*is_array*/ true, GLSL_TYPE_FLOAT);

   nir_variable *img_src = nir_variable_create(b.shader, nir_var_image, src_img_type, "img_src");
   img_src->data.binding = 0;

   nir_variable *img_dst = nir_variable_create(b.shader, nir_var_image, dst_img_type, "img_dst");
   img_dst->data.binding = 1;

   nir_ssa_def *undef32 = nir_ssa_undef(&b, 1, 32);
   nir_ssa_def *zero = nir_imm_int(&b, 0);

   nir_ssa_def *data = nir_image_deref_load(&b, /*num_components*/ 4, /*bit_size*/ 32,
      deref_ssa(&b, img_src), coord_src, undef32, zero);

   nir_image_deref_store(&b, deref_ssa(&b, img_dst), coord_dst, undef32, data, zero);

   return create_shader_state(sctx, b.shader);
}

void *si_create_dcc_retile_cs(struct si_context *sctx, struct radeon_surf *surf)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "dcc_retile");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 3;
   b.shader->info.num_ssbos = 1;

   /* Get user data SGPRs. */
   nir_ssa_def *user_sgprs = nir_load_user_data_amd(&b);

   /* Relative offset from the displayable DCC to the non-displayable DCC in the same buffer. */
   nir_ssa_def *src_dcc_offset = nir_channel(&b, user_sgprs, 0);

   nir_ssa_def *src_dcc_pitch, *dst_dcc_pitch, *src_dcc_height, *dst_dcc_height;
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 1), &src_dcc_pitch, &src_dcc_height);
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 2), &dst_dcc_pitch, &dst_dcc_height);

   /* Get the 2D coordinates. */
   nir_ssa_def *coord = get_global_ids(&b, 2);
   nir_ssa_def *zero = nir_imm_int(&b, 0);

   /* Multiply the coordinates by the DCC block size (they are DCC block coordinates). */
   coord = nir_imul(&b, coord, nir_imm_ivec2(&b, surf->u.gfx9.color.dcc_block_width,
                                             surf->u.gfx9.color.dcc_block_height));

   nir_ssa_def *src_offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, surf->bpe, &surf->u.gfx9.color.dcc_equation,
                                 src_dcc_pitch, src_dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 zero, zero, zero); /* z, sample, pipe_xor */
   src_offset = nir_iadd(&b, src_offset, src_dcc_offset);
   nir_ssa_def *value = nir_load_ssbo(&b, 1, 8, zero, src_offset, .align_mul=1);

   nir_ssa_def *dst_offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, surf->bpe, &surf->u.gfx9.color.display_dcc_equation,
                                 dst_dcc_pitch, dst_dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 zero, zero, zero); /* z, sample, pipe_xor */
   nir_store_ssbo(&b, value, zero, dst_offset, .write_mask=0x1, .align_mul=1);

   return create_shader_state(sctx, b.shader);
}

void *gfx9_create_clear_dcc_msaa_cs(struct si_context *sctx, struct si_texture *tex)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_dcc_msaa");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 2;
   b.shader->info.num_ssbos = 1;

   /* Get user data SGPRs. */
   nir_ssa_def *user_sgprs = nir_load_user_data_amd(&b);
   nir_ssa_def *dcc_pitch, *dcc_height, *clear_value, *pipe_xor;
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 0), &dcc_pitch, &dcc_height);
   unpack_2x16(&b, nir_channel(&b, user_sgprs, 1), &clear_value, &pipe_xor);
   clear_value = nir_u2u16(&b, clear_value);

   /* Get the 2D coordinates. */
   nir_ssa_def *coord = get_global_ids(&b, 3);
   nir_ssa_def *zero = nir_imm_int(&b, 0);

   /* Multiply the coordinates by the DCC block size (they are DCC block coordinates). */
   coord = nir_imul(&b, coord,
                    nir_channels(&b, nir_imm_ivec4(&b, tex->surface.u.gfx9.color.dcc_block_width,
                                                   tex->surface.u.gfx9.color.dcc_block_height,
                                                   tex->surface.u.gfx9.color.dcc_block_depth, 0), 0x7));

   nir_ssa_def *offset =
      ac_nir_dcc_addr_from_coord(&b, &sctx->screen->info, tex->surface.bpe,
                                 &tex->surface.u.gfx9.color.dcc_equation,
                                 dcc_pitch, dcc_height, zero, /* DCC slice size */
                                 nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), /* x, y */
                                 tex->buffer.b.b.array_size > 1 ? nir_channel(&b, coord, 2) : zero, /* z */
                                 zero, pipe_xor); /* sample, pipe_xor */

   /* The trick here is that DCC elements for an even and the next odd sample are next to each other
    * in memory, so we only need to compute the address for sample 0 and the next DCC byte is always
    * sample 1. That's why the clear value has 2 bytes - we're clearing 2 samples at the same time.
    */
   nir_store_ssbo(&b, clear_value, zero, offset, .write_mask=0x1, .align_mul=2);

   return create_shader_state(sctx, b.shader);
}

/* Create a compute shader implementing clear_buffer or copy_buffer. */
void *si_create_clear_buffer_rmw_cs(struct si_context *sctx)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "clear_buffer_rmw_cs");
   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;
   b.shader->info.cs.user_data_components_amd = 2;
   b.shader->info.num_ssbos = 1;

   /* address = blockID * 64 + threadID; */
   nir_ssa_def *address = get_global_ids(&b, 1);

   /* address = address * 16; (byte offset, loading one vec4 per thread) */
   address = nir_ishl(&b, address, nir_imm_int(&b, 4));
   
   nir_ssa_def *zero = nir_imm_int(&b, 0);
   nir_ssa_def *data = nir_load_ssbo(&b, 4, 32, zero, address, .align_mul = 4);

   /* Get user data SGPRs. */
   nir_ssa_def *user_sgprs = nir_load_user_data_amd(&b);

   /* data &= inverted_writemask; */
   data = nir_iand(&b, data, nir_channel(&b, user_sgprs, 1));
   /* data |= clear_value_masked; */
   data = nir_ior(&b, data, nir_channel(&b, user_sgprs, 0));

   nir_store_ssbo(&b, data, zero, address,
      .access = SI_COMPUTE_DST_CACHE_POLICY != L2_LRU ? ACCESS_STREAM_CACHE_POLICY : 0,
      .align_mul = 4);

   return create_shader_state(sctx, b.shader);
}

/* This is used when TCS is NULL in the VS->TCS->TES chain. In this case,
 * VS passes its outputs to TES directly, so the fixed-function shader only
 * has to write TESSOUTER and TESSINNER.
 */
void *si_create_passthrough_tcs(struct si_context *sctx)
{
   const nir_shader_compiler_options *options =
      sctx->b.screen->get_compiler_options(sctx->b.screen, PIPE_SHADER_IR_NIR,
                                           PIPE_SHADER_TESS_CTRL);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_TESS_CTRL, options,
                                                  "tcs passthrough");

   unsigned num_inputs = 0;
   unsigned num_outputs = 0;

   nir_variable *in_inner =
      nir_variable_create(b.shader, nir_var_system_value, glsl_vec_type(2),
                          "tess inner default");
   in_inner->data.location = SYSTEM_VALUE_TESS_LEVEL_INNER_DEFAULT;

   nir_variable *out_inner =
      nir_variable_create(b.shader, nir_var_shader_out, glsl_vec_type(2),
                          "tess inner");
   out_inner->data.location = VARYING_SLOT_TESS_LEVEL_INNER;
   out_inner->data.driver_location = num_outputs++;

   nir_ssa_def *inner = nir_load_var(&b, in_inner);
   nir_store_var(&b, out_inner, inner, 0x3);

   nir_variable *in_outer =
      nir_variable_create(b.shader, nir_var_system_value, glsl_vec4_type(),
                          "tess outer default");
   in_outer->data.location = SYSTEM_VALUE_TESS_LEVEL_OUTER_DEFAULT;

   nir_variable *out_outer =
      nir_variable_create(b.shader, nir_var_shader_out, glsl_vec4_type(),
                          "tess outer");
   out_outer->data.location = VARYING_SLOT_TESS_LEVEL_OUTER;
   out_outer->data.driver_location = num_outputs++;

   nir_ssa_def *outer = nir_load_var(&b, in_outer);
   nir_store_var(&b, out_outer, outer, 0xf);

   nir_ssa_def *id = nir_load_invocation_id(&b);
   struct si_shader_info *info = &sctx->shader.vs.cso->info;
   for (unsigned i = 0; i < info->num_outputs; i++) {
      const struct glsl_type *type;
      unsigned semantic = info->output_semantic[i];
      if (semantic < VARYING_SLOT_VAR31 && semantic != VARYING_SLOT_EDGE)
         type = glsl_array_type(glsl_vec4_type(), 0, 0);
      else if (semantic >= VARYING_SLOT_VAR0_16BIT)
         type = glsl_array_type(glsl_vector_type(GLSL_TYPE_FLOAT16, 4), 0, 0);
      else
         continue;

      char name[10];
      snprintf(name, sizeof(name), "in_%u", i);
      nir_variable *in = nir_variable_create(b.shader, nir_var_shader_in, type, name);
      in->data.location = semantic;
      in->data.driver_location = num_inputs++;

      snprintf(name, sizeof(name), "out_%u", i);
      nir_variable *out = nir_variable_create(b.shader, nir_var_shader_out, type, name);
      out->data.location = semantic;
      out->data.driver_location = num_outputs++;

      /* no need to use copy_var to save a lower pass */
      nir_ssa_def *value = nir_load_array_var(&b, in, id);
      nir_store_array_var(&b, out, id, value, 0xf);
   }

   b.shader->num_inputs = num_inputs;
   b.shader->num_outputs = num_outputs;

   b.shader->info.tess.tcs_vertices_out = sctx->patch_vertices;

   return create_shader_state(sctx, b.shader);
}
