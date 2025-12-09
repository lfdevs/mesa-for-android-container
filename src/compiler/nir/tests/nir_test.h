/*
 * Copyright © 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef NIR_TESTS_NIR_TEST_H
#define NIR_TESTS_NIR_TEST_H

#include <gtest/gtest.h>

#include "nir.h"
#include "nir_builder.h"

#include "util/memstream.h"

struct nir_reference_shader {
   const char *string;
   const char *file;
};

class nir_test : public ::testing::Test {
 protected:
   nir_test(const char *name)
      : nir_test(name, MESA_SHADER_COMPUTE)
   {
   }

   nir_test(const char *name, mesa_shader_stage stage)
   {
      glsl_type_singleton_init_or_ref();

      _b = nir_builder_init_simple_shader(stage, &options, "%s", name);
      b = &_b;

      b->shader->info.next_stage = MESA_SHADER_NONE;
      b->shader->info.internal = false;
   }

   virtual ~nir_test()
   {
      if (HasFailure()) {
         printf("\nShader from the failed test:\n\n");
         nir_print_shader(b->shader, stdout);
      }

      ralloc_free(b->shader);

      glsl_type_singleton_decref();
   }

   /** Compares the NIR shader against the string of a reference shader. */
   void check_nir_string(struct nir_reference_shader reference)
   {
      nir_index_ssa_defs(b->impl);

      char *result = NULL;
      size_t result_size = 0;
      struct u_memstream mem;
      if (!u_memstream_open(&mem, &result, &result_size)) 
         return;

      FILE * memf = u_memstream_get(&mem);
      nir_print_shader(b->shader, memf);
      u_memstream_close(&mem);

      const char *expected = reference.string;

      /* Use a custom comparison that ignores spaces since spaces are not
       * relevant for correctness. This is also intended to be used when
       * specifying the expected shader for indentation. Indentation can
       * not simply be handled by nir_print_shader because that would require
       * computing the indentation.
       */
      const char *result_iter = result;
      size_t result_length = strlen(result);
      const char *expected_iter = expected;
      size_t expected_length = strlen(expected);

      bool failed = false;

      while (result_iter < result + result_length && expected_iter < expected + expected_length) {
         if (isspace(*result_iter)) {
            result_iter++;
            continue;
         }

         if (isspace(*expected_iter)) {
            expected_iter++;
            continue;
         }

         if (*result_iter != *expected_iter) {
            ADD_FAILURE();
            failed = true;
            break;
         }

         result_iter++;
         expected_iter++;
      }

      if (!failed) {
         for (; result_iter < result + result_length; result_iter++) {
            if (!isspace(*result_iter)) {
               ADD_FAILURE();
               failed = true;
               break;
            }
         }
      }

      if (!failed) {
         for (; expected_iter < expected + expected_length; expected_iter++) {
            if (!isspace(*expected_iter)) {
               ADD_FAILURE();
               failed = true;
               break;
            }
         }
      }

      if (failed || debug_get_bool_option("NIR_TEST_DUMP_SHADERS", false))
         printf("BEGIN RESULT\n%s\nEND\nBEGIN EXPECTED(%s)\n%s\nEND\n", result, reference.file, expected);

      free(result);
   }

   nir_shader_compiler_options options = {};
   nir_builder _b;
   nir_builder *b;
};

#define NIR_REFERENCE_SHADER(expected) nir_reference_shader{.string = expected, .file = __FILE__}

#endif
