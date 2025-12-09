/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gtest/gtest.h>
#include "brw_shader.h"
#include "brw_builder.h"

#include <memory>
#include <vector>

std::string brw_shader_to_string(brw_shader *s);

void brw_reindex_vgrfs(brw_shader *s);

/* Note: the order of instructions must be the same for shaders to match
 * so be careful when using patterns like bld.ADD(bld.MOV(), bld.MOV())
 * in the tests since order of argument evaluation is unspecified.
 */
void EXPECT_SHADERS_MATCH(brw_shader *a, brw_shader *b);

inline void
EXPECT_SHADERS_MATCH(brw_builder &a, brw_builder &b)
{
   EXPECT_SHADERS_MATCH(a.shader, b.shader);
}

template <typename PASS>
inline void
EXPECT_PROGRESS_RESULT(bool should_progress, PASS pass, brw_shader *s)
{
   if (!s->cfg)
      brw_calculate_cfg(*s);

   brw_validate(*s);

   auto before = brw_shader_to_string(s);
   bool progress = pass(*s);
   auto after = brw_shader_to_string(s);

   brw_validate(*s);

   if (progress != should_progress || os_get_option("TEST_DEBUG")) {
      fprintf(stderr, "= Before =\n"
                      "%s\n"
                      "\n"
                      "= After =\n"
                      "%s\n",
              before.c_str(),
              after.c_str());
   }

   EXPECT_EQ(progress, should_progress);
   if (should_progress)
      EXPECT_NE(before, after);
   else
      EXPECT_EQ(before, after);
}

template <typename PASS>
inline void
EXPECT_PROGRESS(PASS pass, brw_shader *s)
{
   EXPECT_PROGRESS_RESULT(true, pass, s);
}

template <typename PASS>
inline void
EXPECT_NO_PROGRESS(PASS pass, brw_shader *s)
{
   EXPECT_PROGRESS_RESULT(false, pass, s);
}

template <typename PASS>
inline void
EXPECT_PROGRESS_RESULT(bool should_progress, PASS pass, brw_builder &bld)
{
   EXPECT_PROGRESS_RESULT(should_progress, pass, bld.shader);
}

template <typename PASS>
inline void
EXPECT_PROGRESS(PASS pass, brw_builder &bld)
{
   EXPECT_PROGRESS(pass, bld.shader);
}

template <typename PASS>
inline void
EXPECT_NO_PROGRESS(PASS pass, brw_builder &bld)
{
   EXPECT_NO_PROGRESS(pass, bld.shader);
}

class brw_shader_pass_test : public ::testing::Test {
protected:
   void *mem_ctx;
   struct brw_compiler *compiler;
   struct intel_device_info *devinfo;

   /* TODO: Once brw_shader is ralloc-able, we can simply get rid of this. */
   std::vector<std::unique_ptr<brw_shader>> shaders;

   brw_shader_pass_test()
   {
      mem_ctx = ralloc_context(NULL);
      compiler = rzalloc(mem_ctx, struct brw_compiler);
      devinfo = rzalloc(mem_ctx, struct intel_device_info);
      compiler->devinfo = devinfo;

      set_gfx_verx10(90);
   }

   ~brw_shader_pass_test() override
   {
      ralloc_free(mem_ctx);
   }

   void
   set_gfx_verx10(unsigned verx10)
   {
      devinfo->verx10 = verx10;
      devinfo->ver    = verx10 / 10;
      assert(devinfo->ver > 0);
      brw_init_isa_info(&compiler->isa, devinfo);
   }

   brw_builder
   make_shader(mesa_shader_stage stage = MESA_SHADER_FRAGMENT,
               unsigned dispatch_width = 0)
   {
      if (dispatch_width == 0)
         dispatch_width = devinfo->ver >= 20 ? 16 : 8;

      nir_shader *nir = nir_shader_create(mem_ctx, stage, NULL);
      brw_stage_prog_data *pd =
         (struct brw_stage_prog_data *)rzalloc(mem_ctx, brw_any_prog_data);

      const brw_shader_params shader_params = {
         .compiler       = compiler,
         .mem_ctx        = mem_ctx,
         .nir            = nir,
         .prog_data      = pd,
         .dispatch_width = dispatch_width,
      };
      shaders.push_back(std::make_unique<brw_shader>(&shader_params));

      brw_shader *s = shaders.back().get();
      s->phase = BRW_SHADER_PHASE_AFTER_OPT_LOOP;
      return brw_builder(s);
   }

   /* Helper to allocate same vgrf number in two shaders. */
   static brw_reg
   vgrf(brw_shader *a, brw_shader *b,
        brw_reg_type type, unsigned n = 1,
        unsigned dispatch_width = 0)
   {
      assert(a && b && a->dispatch_width == b->dispatch_width);

      if (dispatch_width == 0)
         dispatch_width = a->dispatch_width;

      brw_reg reg_a = brw_allocate_vgrf(*a, type, n * dispatch_width);
      brw_reg reg_b = brw_allocate_vgrf(*b, type, n * dispatch_width);
      assert(brw_regs_equal(&reg_a, &reg_b));

      return reg_a;
   }

   static brw_reg
   vgrf(brw_builder &a, brw_builder &b,
        brw_reg_type type, unsigned n = 1,
        unsigned dispatch_width = 0)
   {
      return vgrf(a.shader, b.shader, type, n, dispatch_width);
   }

   static brw_reg
   vgrf(brw_shader *a,
        brw_reg_type type, unsigned n = 1,
        unsigned dispatch_width = 0)
   {
      assert(a);

      if (dispatch_width == 0)
         dispatch_width = a->dispatch_width;

      return brw_allocate_vgrf(*a, type, n * dispatch_width);
   }

   static brw_reg
   vgrf(brw_builder &a,
        brw_reg_type type, unsigned n = 1,
        unsigned dispatch_width = 0)
   {
      return vgrf(a.shader, type, n, dispatch_width);
   }
};
