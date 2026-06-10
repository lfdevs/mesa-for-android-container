/*
 * Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "ac_nir.h"
#include "nir.h"
#include "nir_builder.h"

class lower_tex_coords_test : public ::testing::Test {
protected:
   lower_tex_coords_test()
   {
      glsl_type_singleton_init_or_ref();
   }

   ~lower_tex_coords_test()
   {
      glsl_type_singleton_decref();
   }

   nir_builder b;
};

static nir_def *
load_vec2_input(nir_builder *b, nir_def *bary, int base)
{
   nir_io_semantics semantics = {0};
   semantics.location = VARYING_SLOT_VAR0 + base;
   semantics.num_slots = 1;
   semantics.medium_precision = true;

   nir_def *zero = nir_imm_int(b, 0);

   nir_def *x =
      nir_load_interpolated_input(b, 1, 32, bary, zero,
                                  base, 0, nir_type_float32, semantics);

   nir_def *y =
      nir_load_interpolated_input(b, 1, 32, bary, zero,
                                  base, 1, nir_type_float32, semantics);

   return nir_vec2(b, x, y);
}

static void
create_shader(nir_builder *b)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(b->shader);
   b->cursor = nir_before_impl(impl);

   nir_variable *tex0 =
      nir_variable_create(b->shader, nir_var_uniform,
                          glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT),
                          "u_tex0");

   nir_variable *tex1 =
      nir_variable_create(b->shader, nir_var_uniform,
                          glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT),
                          "u_tex1");

   tex0->data.binding = 0;
   tex1->data.binding = 1;

   nir_def *zero = nir_imm_int(b, 0);

   nir_def *bary =
      nir_load_barycentric_pixel(b, 32,
                                 INTERP_MODE_SMOOTH);

   /*
    * Create a condition which becomes divergent and feeds terminate_if.
    */
   nir_io_semantics semantics = {};
   semantics.location = VARYING_SLOT_VAR2;
   semantics.num_slots = 1;

   nir_def *clip0 =
      nir_load_interpolated_input(b, 1, 32, bary, zero, 2,
                                  0, nir_type_float32, semantics);

   /*
    * Divergent terminate_if.
    */
   nir_def *cond = nir_flt_imm(b, clip0, 0.0f);
   nir_terminate_if(b, cond);

   /*
    * First texture coordinate.
    */
   nir_def *coord0 = load_vec2_input(b, bary, 1);

   /*
    * Second texture coordinate.
    */
   nir_def *coord1 = load_vec2_input(b, bary, 2);

   /*
    * Texture sample using first coord.
    */
   nir_tex_instr *tex_instr0 =
      nir_tex_instr_create(b->shader, 2);

   tex_instr0->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex_instr0->dest_type = nir_type_float32;
   tex_instr0->coord_components = 2;
   tex_instr0->is_array = false;

   tex_instr0->src[0].src_type = nir_tex_src_coord;
   tex_instr0->src[0].src = nir_src_for_ssa(coord0);


   nir_deref_instr *tex0_deref =
      nir_build_deref_var(b, tex0);
   tex_instr0->src[1].src_type = nir_tex_src_texture_deref;
   tex_instr0->src[1].src =
      nir_src_for_ssa(&tex0_deref->def);

   tex_instr0->texture_index = 0;
   tex_instr0->sampler_index = 0;

   nir_def_init(&tex_instr0->instr, &tex_instr0->def, 4, 32);

   nir_builder_instr_insert(b, &tex_instr0->instr);

   /*
    * Texture sample using second coord.
    */
   nir_tex_instr *tex_instr1 =
      nir_tex_instr_create(b->shader, 2);

   tex_instr1->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex_instr1->dest_type = nir_type_float32;
   tex_instr1->coord_components = 2;
   tex_instr1->is_array = false;

   tex_instr1->src[0].src_type = nir_tex_src_coord;
   tex_instr1->src[0].src = nir_src_for_ssa(coord1);

   nir_deref_instr *tex1_deref =
      nir_build_deref_var(b, tex1);
   tex_instr1->src[1].src_type = nir_tex_src_texture_deref;
   tex_instr1->src[1].src =
      nir_src_for_ssa(&tex1_deref->def);

   tex_instr1->texture_index = 1;
   tex_instr1->sampler_index = 1;

   nir_def_init(&tex_instr1->instr, &tex_instr1->def, 4, 32);

   nir_builder_instr_insert(b, &tex_instr1->instr);

   /*
    * Simple output to keep both texture instructions alive.
    */
   nir_def *mul =
      nir_fmul(b,
               nir_channel(b, &tex_instr0->def, 0),
               nir_channel(b, &tex_instr1->def, 0));

   nir_io_semantics fd_semantics = {};
   fd_semantics.location = FRAG_RESULT_DATA0;
   fd_semantics.num_slots = 1;

   nir_store_output(b, mul, zero, 0, 0, 0x1, 0, nir_type_float32, fd_semantics);
}

TEST_F(lower_tex_coords_test, multiple_strict_wqm_coord_with_terminate_if)
{
   static nir_shader_compiler_options options = {};

   b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                      &options,
                                      "tex coords");
   create_shader(&b);

   ac_nir_lower_tex_coords_options opts = {};
   opts.gfx_level = GFX8;
   opts.lower_array_layer_round_even = true;
   opts.fix_derivs_in_divergent_cf = true;
   opts.max_wqm_vgprs = 64;

   bool progress = ac_nir_lower_tex_coords(b.shader, &opts);

   nir_validate_shader(b.shader, "after lowering tex coords");

   unsigned strict_wqm_coord_count = 0;
   unsigned terminate_if_count = 0;
   unsigned tex_count = 0;

   nir_function_impl *impl =
      nir_shader_get_entrypoint(b.shader);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intrin =
               nir_instr_as_intrinsic(instr);

            if (intrin->intrinsic ==
                nir_intrinsic_strict_wqm_coord_amd)
               strict_wqm_coord_count++;

            if (intrin->intrinsic ==
                nir_intrinsic_terminate_if)
               terminate_if_count++;
         }

         if (instr->type == nir_instr_type_tex)
            tex_count++;
      }
   }

   EXPECT_TRUE(progress);
   EXPECT_EQ(strict_wqm_coord_count, 2u);
   EXPECT_EQ(terminate_if_count, 1u);
   EXPECT_EQ(tex_count, 2u);

   ralloc_free(b.shader);
}
