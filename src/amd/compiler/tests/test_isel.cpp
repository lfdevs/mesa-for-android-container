/*
 * Copyright © 2020 Valve Corporation
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
 *
 */
#include "helpers.h"
#include "test_isel-spirv.h"

using namespace aco;

BEGIN_TEST(isel.interp.simple)
   QoShaderModuleCreateInfo vs = qoShaderModuleCreateInfoGLSL(VERTEX,
      layout(location = 0) in vec4 in_color;
      layout(location = 0) out vec4 out_color;
      void main() {
         out_color = in_color;
      }
   );
   QoShaderModuleCreateInfo fs = qoShaderModuleCreateInfoGLSL(FRAGMENT,
      layout(location = 0) in vec4 in_color;
      layout(location = 0) out vec4 out_color;
      void main() {
         //>> v1: %a_tmp = v_interp_p1_f32 %bx, %pm:m0 attr0.w
         //! v1: %a = v_interp_p2_f32 %by, %pm:m0, %a_tmp attr0.w
         //! v1: %b_tmp = v_interp_p1_f32 %bx, %pm:m0 attr0.z
         //! v1: %b = v_interp_p2_f32 %by, %pm:m0, %b_tmp attr0.z
         //! v1: %g_tmp = v_interp_p1_f32 %bx, %pm:m0 attr0.y
         //! v1: %g = v_interp_p2_f32 %by, %pm:m0, %g_tmp attr0.y
         //! v1: %r_tmp = v_interp_p1_f32 %bx, %pm:m0 attr0.x
         //! v1: %r = v_interp_p2_f32 %by, %pm:m0, %r_tmp attr0.x
         //! exp %r, %g, %b, %a mrt0
         out_color = in_color;
      }
   );

   PipelineBuilder bld(get_vk_device(GFX9));
   bld.add_vsfs(vs, fs);
   bld.print_ir(VK_SHADER_STAGE_FRAGMENT_BIT, "ACO IR");
END_TEST

BEGIN_TEST(isel.compute.simple)
   for (unsigned i = GFX7; i <= GFX8; i++) {
      if (!set_variant((chip_class)i))
         continue;

      QoShaderModuleCreateInfo cs = qoShaderModuleCreateInfoGLSL(COMPUTE,
         layout(local_size_x=1) in;
         layout(binding=0) buffer Buf {
            uint res;
         };
         void main() {
            //~gfx7>> v1: %data = p_parallelcopy 42
            //~gfx7>> buffer_store_dword %_, v1: undef, 0, %data disable_wqm storage:buffer semantics: scope:invocation
            //~gfx8>> s1: %data = p_parallelcopy 42
            //~gfx8>> s_buffer_store_dword %_, 0, %data storage:buffer semantics: scope:invocation
            res = 42;
         }
      );

      PipelineBuilder bld(get_vk_device((chip_class)i));
      bld.add_cs(cs);
      bld.print_ir(VK_SHADER_STAGE_COMPUTE_BIT, "ACO IR", true);
   }
END_TEST
