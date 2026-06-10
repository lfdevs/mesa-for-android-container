/*
 * Copyright © 2026 Valve Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <gtest/gtest.h>
#include "nir.h"
#include "nir_builder.h"
#include "nir_range_analysis.h"

#define SRC_VAL_COUNT 17

class fp_class_test : public ::testing::Test {
 protected:
   fp_class_test()
   {
      glsl_type_singleton_init_or_ref();

      static const nir_shader_compiler_options options = {};
      b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &options,
                                         "fp class test");
      b.fp_math_ctrl = nir_fp_no_fast_math;

      range_ht = nir_create_fp_analysis_state(b.impl);

      static const float src_values[SRC_VAL_COUNT] = {
         NAN,
         -INFINITY,
         -FLT_MAX,
         -FLT_MAX / 2,
         -1.00001f,
         -1.0f,
         -0.5f,
         -FLT_MIN * 2,
         -0.0f,
         +0.0f,
         FLT_MIN,
         0.9999f,
         1.0f,
         1.7554f,
         1371.0f,
         FLT_MAX,
         INFINITY,
      };

      for (unsigned i = 0; i < SRC_VAL_COUNT; i++)
         const_src[i] = nir_imm_float(&b, src_values[i]);
   }

   ~fp_class_test()
   {
      nir_free_fp_analysis_state(&range_ht);
      ralloc_free(b.shader);
      glsl_type_singleton_decref();
   }

   void test_op3(nir_op op)
   {
      for (unsigned i = 0; i < SRC_VAL_COUNT; i++) {
         for (unsigned j = 0; j < SRC_VAL_COUNT; j++) {
            for (unsigned k = 0; k < SRC_VAL_COUNT; k++) {
               nir_def *alu = nir_build_alu3(&b, op, const_src[i], const_src[j], const_src[k]);

               nir_invalidate_fp_analysis_state(&range_ht);
               fp_class_mask fp_class_estimate =
                  nir_analyze_fp_class(&range_ht, alu);

               nir_def *res = nir_try_constant_fold_alu(&b, nir_def_as_alu(alu));

               nir_invalidate_fp_analysis_state(&range_ht);
               fp_class_mask fp_class_real =
                  nir_analyze_fp_class(&range_ht, res);

               if ((fp_class_real & fp_class_estimate) != fp_class_real) {
                  fprintf(stderr, "0x%x, 0x%x\n", fp_class_estimate, fp_class_real);
                  fprintf(stderr, "%d, %d, %d\n", i, j, k);
                  fprintf(stderr, "%f, %f, %f= %f\n", nir_scalar_as_float(nir_get_scalar(const_src[i], 0)),
                          nir_scalar_as_float(nir_get_scalar(const_src[j], 0)),
                          nir_scalar_as_float(nir_get_scalar(const_src[k], 0)),
                          nir_scalar_as_float(nir_get_scalar(res, 0)));
                  fprintf(stderr, "0x%x, 0x%x, 0x%x = 0x%x\n", (uint32_t)nir_scalar_as_uint(nir_get_scalar(const_src[i], 0)),
                          (uint32_t)nir_scalar_as_uint(nir_get_scalar(const_src[j], 0)),
                          (uint32_t)nir_scalar_as_uint(nir_get_scalar(const_src[k], 0)),
                          (uint32_t)nir_scalar_as_uint(nir_get_scalar(res, 0)));
               }

               EXPECT_EQ(fp_class_real & fp_class_estimate, fp_class_real);
            }
         }
      }
   }

   void test_op2(nir_op op)
   {
      for (unsigned i = 0; i < SRC_VAL_COUNT; i++) {
         for (unsigned j = 0; j < SRC_VAL_COUNT; j++) {
            nir_def *alu = nir_build_alu2(&b, op, const_src[i], const_src[j]);

            nir_invalidate_fp_analysis_state(&range_ht);
            fp_class_mask fp_class_estimate =
               nir_analyze_fp_class(&range_ht, alu);

            nir_def *res = nir_try_constant_fold_alu(&b, nir_def_as_alu(alu));

            nir_invalidate_fp_analysis_state(&range_ht);
            fp_class_mask fp_class_real =
               nir_analyze_fp_class(&range_ht, res);

            if ((fp_class_real & fp_class_estimate) != fp_class_real) {
               fprintf(stderr, "0x%x, 0x%x\n", fp_class_estimate, fp_class_real);
               fprintf(stderr, "%d, %d\n", i, j);
               fprintf(stderr, "%f, %f = %f\n", nir_scalar_as_float(nir_get_scalar(const_src[i], 0)),
                       nir_scalar_as_float(nir_get_scalar(const_src[j], 0)),
                       nir_scalar_as_float(nir_get_scalar(res, 0)));
               fprintf(stderr, "0x%x, 0x%x = 0x%x\n", (uint32_t)nir_scalar_as_uint(nir_get_scalar(const_src[i], 0)),
                       (uint32_t)nir_scalar_as_uint(nir_get_scalar(const_src[j], 0)),
                       (uint32_t)nir_scalar_as_uint(nir_get_scalar(res, 0)));
            }

            EXPECT_EQ(fp_class_real & fp_class_estimate, fp_class_real);
         }
      }
   }

   void test_op1(nir_op op)
   {
      for (unsigned i = 0; i < SRC_VAL_COUNT; i++) {
         nir_def *alu = nir_build_alu1(&b, op, const_src[i]);

         nir_invalidate_fp_analysis_state(&range_ht);
         fp_class_mask fp_class_estimate =
            nir_analyze_fp_class(&range_ht, alu);

         nir_def *res = nir_try_constant_fold_alu(&b, nir_def_as_alu(alu));

         nir_invalidate_fp_analysis_state(&range_ht);
         fp_class_mask fp_class_real =
            nir_analyze_fp_class(&range_ht, res);

         if ((fp_class_real & fp_class_estimate) != fp_class_real) {
            fprintf(stderr, "0x%x, 0x%x\n", fp_class_estimate, fp_class_real);
            fprintf(stderr, "%d\n", i);
            fprintf(stderr, "%f = %f\n", nir_scalar_as_float(nir_get_scalar(const_src[i], 0)),
                    nir_scalar_as_float(nir_get_scalar(res, 0)));
            fprintf(stderr, "0x%x = 0x%x\n", (uint32_t)nir_scalar_as_uint(nir_get_scalar(const_src[i], 0)),
                    (uint32_t)nir_scalar_as_uint(nir_get_scalar(res, 0)));
         }

         EXPECT_EQ(fp_class_real & fp_class_estimate, fp_class_real);
      }
   }

   struct nir_builder b;
   nir_fp_analysis_state range_ht;
   nir_def *const_src[SRC_VAL_COUNT];
};

#define DEFINE_TEST(op, arity)          \
   TEST_F(fp_class_test, fp_class_##op) \
   {                                    \
      test_op##arity(nir_op_##op);      \
   }

DEFINE_TEST(fadd, 2)
DEFINE_TEST(fsub, 2)
DEFINE_TEST(fmin, 2)
DEFINE_TEST(fmax, 2)
DEFINE_TEST(fmul, 2)
DEFINE_TEST(fmulz, 2)
DEFINE_TEST(fpow, 2)
DEFINE_TEST(fdot2, 2)
DEFINE_TEST(ffma, 3)
DEFINE_TEST(ffma_weak, 3)
DEFINE_TEST(ffmaz, 3)
DEFINE_TEST(fmad, 3)
DEFINE_TEST(fmadz, 3)
DEFINE_TEST(fabs, 1)
DEFINE_TEST(fneg, 1)
DEFINE_TEST(fexp2, 1)
DEFINE_TEST(flog2, 1)
DEFINE_TEST(frcp, 1)
DEFINE_TEST(fsqrt, 1)
DEFINE_TEST(frsq, 1)
DEFINE_TEST(fsat, 1)
DEFINE_TEST(fsign, 1)
DEFINE_TEST(ffloor, 1)
DEFINE_TEST(fceil, 1)
DEFINE_TEST(ftrunc, 1)
DEFINE_TEST(fround_even, 1)
DEFINE_TEST(ffract, 1)
DEFINE_TEST(fsin, 1)
DEFINE_TEST(fcos, 1)
DEFINE_TEST(f2f16_rtz, 1)
DEFINE_TEST(f2f16_rtne, 1)
DEFINE_TEST(f2f64, 1)
