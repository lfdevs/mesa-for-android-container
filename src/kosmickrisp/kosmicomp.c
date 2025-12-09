/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "compiler/nir_to_msl.h"
#include "spirv/nir_spirv.h"

static int
load_spirv(const char *filename, uint32_t **words, size_t *nwords)
{
   const size_t CHUNK_SIZE = 4096;
   uint32_t buf[CHUNK_SIZE];
   FILE *input = fopen(filename, "r");
   if (!input) {
      fprintf(stderr, "Could not open file %s: %s\n", filename,
              strerror(errno));
      return -1;
   }

   *nwords = 0;
   *words = malloc(CHUNK_SIZE * sizeof(buf[0]));
   size_t read_size;
   while (1) {
      read_size = fread(buf, sizeof(buf[0]), CHUNK_SIZE, input);
      if (read_size == 0)
         break;
      *words = realloc(*words, (*nwords + read_size) * sizeof(buf[0]));
      memcpy(*words + *nwords, buf, sizeof(buf[0]) * read_size);
      *nwords += read_size;
   };

   if (*words[0] != 0x07230203) {
      fprintf(stderr, "%s is not a SPIR-V file?\n", filename);
      return -1;
   }

   return 0;
}

static void
debug_callback(void *priv, enum nir_spirv_debug_level debuglevel, size_t offset,
               const char *message)
{
   fprintf(stderr, "<%d> at %ld %s\n", debuglevel, offset, message);
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size;
}

static void
optimize(nir_shader *nir)
{
   msl_preprocess_nir(nir);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);
   NIR_PASS(_, nir, nir_lower_explicit_io,
            nir_var_mem_global | nir_var_mem_ubo | nir_var_mem_ssbo,
            nir_address_format_64bit_global);
   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      if (!nir->info.shared_memory_explicit_layout) {
         /* There may be garbage in shared_size, but it's the job of
          * nir_lower_vars_to_explicit_types to allocate it. We have to reset to
          * avoid overallocation.
          */
         nir->info.shared_size = 0;

         NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
                  shared_var_info);
      }
      NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
               nir_address_format_32bit_offset);
   }

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            type_size_vec4, (nir_lower_io_options)0);

   NIR_PASS(_, nir, nir_lower_variable_initializers, ~nir_var_function_temp);
   NIR_PASS(_, nir, nir_remove_dead_variables,
            nir_var_shader_in | nir_var_shader_out | nir_var_system_value,
            NULL);
   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);
   nir_lower_compute_system_values_options options = {
      .has_base_global_invocation_id = 0,
   };
   NIR_PASS(_, nir, nir_lower_system_values);
   NIR_PASS(_, nir, nir_lower_compute_system_values, &options);
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);

   msl_optimize_nir(nir);
}

static mesa_shader_stage
stage_from_filename(const char *filename)
{
   struct StageMapping {
      char *name;
      mesa_shader_stage stage;
   };
   struct StageMapping stage_mappings[] = {
      {.name = ".frag.", .stage = MESA_SHADER_FRAGMENT},
      {.name = ".vert.", .stage = MESA_SHADER_VERTEX},
      {.name = ".comp.", .stage = MESA_SHADER_COMPUTE},
   };
   for (int i = 0; i < ARRAY_SIZE(stage_mappings); i++) {
      if (strstr(filename, stage_mappings[i].name))
         return stage_mappings[i].stage;
   }
   return MESA_SHADER_NONE;
}

int
main(int argc, char **argv)
{
   if (argc != 2) {
      fprintf(stderr, "Usage: kosmicomp filename.spv\n");
      return 1;
   }

   // read file
   size_t nwords = 0;
   uint32_t *words = NULL;
   int result = load_spirv(argv[1], &words, &nwords);
   if (result == -1) {
      return 2;
   }

   // run spirv_to_nir
   struct spirv_to_nir_options options = {
      .environment = NIR_SPIRV_VULKAN,
      .debug =
         {
            .func = &debug_callback,
            .private_data = NULL,
         },
      .ubo_addr_format = nir_address_format_64bit_global,
      .ssbo_addr_format = nir_address_format_64bit_global,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
   };
   glsl_type_singleton_init_or_ref();
   struct nir_shader_compiler_options nir_options = {
      .lower_fdph = 1,
   };
   mesa_shader_stage stage = stage_from_filename(argv[1]);
   if (stage == MESA_SHADER_NONE) {
      fprintf(stderr, "Couldn't guess shader stage from %s\n", argv[1]);
      return 4;
   }
   nir_shader *shader = spirv_to_nir(words, nwords, NULL, 0, stage, "main",
                                     &options, &nir_options);
   if (!shader) {
      fprintf(stderr, "Compilation failed!\n");
      return 3;
   }
   // print nir
   nir_print_shader(shader, stdout);
   optimize(shader);
   nir_print_shader(shader, stdout);

   char *msl_text = nir_to_msl(shader, shader, 0u);

   fputs(msl_text, stdout);

   ralloc_free(msl_text);

   return 0;
}
