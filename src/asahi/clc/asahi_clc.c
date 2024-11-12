/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "asahi/compiler/agx_compile.h"
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_serialize.h"

#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "util/u_math.h"
#include <sys/mman.h>

static const struct spirv_to_nir_options spirv_options = {
   .environment = NIR_SPIRV_OPENCL,
   .shared_addr_format = nir_address_format_62bit_generic,
   .global_addr_format = nir_address_format_62bit_generic,
   .temp_addr_format = nir_address_format_62bit_generic,
   .constant_addr_format = nir_address_format_64bit_global,
   .create_library = true,
};

static bool
lower_builtins(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_call)
      return false;

   nir_call_instr *call = nir_instr_as_call(instr);
   nir_function *func = call->callee;

   if (strcmp(func->name, "nir_interleave_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(
         b, nir_src_as_deref(call->params[0]),
         nir_interleave_agx(b, call->params[1].ssa, call->params[2].ssa), 1);

      return true;
   } else if (strcmp(func->name, "nir_doorbell_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_doorbell_agx(b, call->params[0].ssa);
      return true;
   } else if (strcmp(func->name, "nir_stack_map_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_stack_map_agx(b, call->params[0].ssa, call->params[1].ssa);
      return true;
   } else if (strcmp(func->name, "nir_stack_unmap_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(b, nir_src_as_deref(call->params[0]),
                      nir_stack_unmap_agx(b, call->params[1].ssa), 1);
      return true;
   } else if (strcmp(func->name, "nir_load_core_id_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(b, nir_src_as_deref(call->params[0]),
                      nir_load_core_id_agx(b), 1);
      return true;
   } else if (strcmp(func->name, "nir_load_helper_op_id_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(b, nir_src_as_deref(call->params[0]),
                      nir_load_helper_op_id_agx(b, 1, 32), 1);
      return true;
   } else if (strcmp(func->name, "nir_load_helper_arg_lo_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(b, nir_src_as_deref(call->params[0]),
                      nir_load_helper_arg_lo_agx(b, 1, 32), 1);
      return true;
   } else if (strcmp(func->name, "nir_load_helper_arg_hi_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(b, nir_src_as_deref(call->params[0]),
                      nir_load_helper_arg_hi_agx(b, 1, 32), 1);
      return true;
   } else if (strcmp(func->name, "ballot") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_store_deref(b, nir_src_as_deref(call->params[0]),
                      nir_ballot(b, 1, 32, call->params[1].ssa), 1);
      return true;
   } else if (strcmp(func->name, "nir_fence_helper_exit_agx") == 0) {
      b->cursor = nir_instr_remove(&call->instr);
      nir_fence_helper_exit_agx(b);
      return true;
   } else if (strcmp(func->name, "nir_bindless_image_load_array") == 0) {
      b->cursor = nir_instr_remove(&call->instr);

      nir_def *texel = nir_bindless_image_load(
         b, 4, 32, call->params[1].ssa, call->params[2].ssa, nir_imm_int(b, 0),
         nir_imm_int(b, 0), .image_array = true,
         .image_dim = GLSL_SAMPLER_DIM_2D, .dest_type = nir_type_uint32,
         .access = ACCESS_IN_BOUNDS_AGX);

      nir_store_deref(b, nir_src_as_deref(call->params[0]), texel, 0xf);
      return true;
   } else if (strcmp(func->name, "nir_bindless_image_store_array") == 0) {
      b->cursor = nir_instr_remove(&call->instr);

      nir_bindless_image_store(
         b, call->params[0].ssa, call->params[1].ssa, nir_imm_int(b, 0),
         call->params[2].ssa, nir_imm_int(b, 0), .image_array = true,
         .image_dim = GLSL_SAMPLER_DIM_2D, .src_type = nir_type_uint32,
         .access = ACCESS_NON_READABLE);
      return true;
   } else if (strcmp(func->name, "nir_bindless_image_load_ms_array") == 0) {
      b->cursor = nir_instr_remove(&call->instr);

      nir_def *texel = nir_bindless_image_load(
         b, 4, 32, call->params[1].ssa, call->params[2].ssa,
         call->params[3].ssa, nir_imm_int(b, 0), .image_array = true,
         .image_dim = GLSL_SAMPLER_DIM_MS, .dest_type = nir_type_uint32,
         .access = ACCESS_IN_BOUNDS_AGX);

      nir_store_deref(b, nir_src_as_deref(call->params[0]), texel, 0xf);
      return true;
   } else if (strcmp(func->name, "nir_bindless_image_store_ms_array") == 0) {
      b->cursor = nir_instr_remove(&call->instr);

      nir_bindless_image_store(
         b, call->params[0].ssa, call->params[1].ssa, call->params[2].ssa,
         call->params[3].ssa, nir_imm_int(b, 0), .image_array = true,
         .image_dim = GLSL_SAMPLER_DIM_MS, .src_type = nir_type_uint32,
         .access = ACCESS_NON_READABLE);
      return true;
   }

   return false;
}

/* Standard optimization loop */
static void
optimize(nir_shader *nir)
{
   bool progress;
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_var_copies);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, true);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 64, false, true);
      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_lower_undef_to_zero);

      NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);

      NIR_PASS(progress, nir, nir_split_var_copies);
      NIR_PASS(progress, nir, nir_split_struct_vars, nir_var_function_temp);
   } while (progress);
}

static nir_shader *
compile(void *memctx, const uint32_t *spirv, size_t spirv_size)
{
   const nir_shader_compiler_options *nir_options = &agx_nir_options;

   assert(spirv_size % 4 == 0);
   nir_shader *nir =
      spirv_to_nir(spirv, spirv_size / 4, NULL, 0, MESA_SHADER_KERNEL,
                   "library", &spirv_options, nir_options);
   nir_validate_shader(nir, "after spirv_to_nir");
   nir_validate_ssa_dominance(nir, "after spirv_to_nir");
   ralloc_steal(memctx, nir);

   NIR_PASS(_, nir, nir_lower_system_values);
   nir_shader_instructions_pass(nir, lower_builtins, nir_metadata_none, NULL);

   /* We have to lower away local constant initializers right before we
    * inline functions.  That way they get properly initialized at the top
    * of the function and not at the top of its caller.
    */
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   nir_remove_non_exported(nir);
   NIR_PASS(_, nir, nir_copy_prop);
   NIR_PASS(_, nir, nir_opt_deref);

   /* We can go ahead and lower the rest of the constant initializers.  We do
    * this here so that nir_remove_dead_variables and split_per_member_structs
    * below see the corresponding stores.
    */
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);

   /* LLVM loves take advantage of the fact that vec3s in OpenCL are 16B
    * aligned and so it can just read/write them as vec4s.  This results in a
    * LOT of vec4->vec3 casts on loads and stores.  One solution to this
    * problem is to get rid of all vec3 variables.
    */
   NIR_PASS(_, nir, nir_lower_vec3_to_vec4,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant);

   /* We assign explicit types early so that the optimizer can take advantage
    * of that information and hopefully get rid of some of our memcpys.
    */
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_uniform | nir_var_shader_temp | nir_var_function_temp |
               nir_var_mem_shared | nir_var_mem_global,
            glsl_get_cl_type_size_align);

   optimize(nir);

   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_all, NULL);

   /* Lower again, this time after dead-variables to get more compact variable
    * layouts.
    */
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant,
            glsl_get_cl_type_size_align);
   if (nir->constant_data_size > 0) {
      assert(nir->constant_data == NULL);
      nir->constant_data = rzalloc_size(nir, nir->constant_data_size);
      nir_gather_explicit_io_initializers(nir, nir->constant_data,
                                          nir->constant_data_size,
                                          nir_var_mem_constant);
   }

   NIR_PASS(_, nir, nir_lower_memcpy);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_constant,
            nir_address_format_64bit_global);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_uniform,
            nir_address_format_32bit_offset_as_64bit);

   /* Note: we cannot lower explicit I/O here, because we need derefs in tact
    * for function calls into the library to work.
    */

   NIR_PASS(_, nir, nir_lower_convert_alu_types, NULL);
   NIR_PASS(_, nir, nir_opt_if, 0);
   NIR_PASS(_, nir, nir_opt_idiv_const, 16);

   optimize(nir);

   return nir;
}

static void
print_u32_data(FILE *fp, const char *prefix, const char *arr_name,
               const uint32_t *data, size_t len)
{
   fprintf(fp, "static const uint32_t %s_%s[] = {", prefix, arr_name);
   for (unsigned i = 0; i < (len / 4); i++) {
      if (i % 4 == 0)
         fprintf(fp, "\n   ");

      fprintf(fp, " 0x%08" PRIx32 ",", data[i]);
   }

   if (len % 4) {
      const uint8_t *data_u8 = (const uint8_t *)data;
      uint32_t last = 0;
      unsigned last_offs = ROUND_DOWN_TO(len, 4);
      for (unsigned i = 0; i < len % 4; ++i) {
         last |= (uint32_t)data_u8[last_offs + i] << (i * 8);
      }

      fprintf(fp, " 0x%08" PRIx32 ",", last);
   }

   fprintf(fp, "\n};\n");
}

static void
print_usage(char *exec_name, FILE *f)
{
   fprintf(
      f,
      "Usage: %s [options] -- [clang args]\n"
      "Options:\n"
      "  -h  --help              Print this help.\n"
      "      --prefix <prefix>   Prefix for variable names in generated C code.\n"
      "  -o, --out <filename>    Specify the output filename.\n"
      "  -i, --in <filename>     Specify one input filename. Accepted multiple times.\n"
      "  -s, --spv <filename>    Specify the output filename for spirv.\n"
      "  -v, --verbose           Print more information during compilation.\n",
      exec_name);
}

#define OPT_PREFIX 1000

int
main(int argc, char **argv)
{
   static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"prefix", required_argument, 0, OPT_PREFIX},
      {"in", required_argument, 0, 'i'},
      {"out", required_argument, 0, 'o'},
      {"verbose", no_argument, 0, 'v'},
      {0, 0, 0, 0},
   };

   char *infile = NULL, *outfile = NULL, *prefix = NULL;
   void *mem_ctx = ralloc_context(NULL);

   int ch;
   while ((ch = getopt_long(argc, argv, "he:p:i:o:v", long_options, NULL)) !=
          -1) {
      switch (ch) {
      case 'h':
         print_usage(argv[0], stdout);
         return 0;
      case 'o':
         outfile = optarg;
         break;
      case 'i':
         infile = optarg;
         break;
      case OPT_PREFIX:
         prefix = optarg;
         break;
      default:
         fprintf(stderr, "Unrecognized option \"%s\".\n", optarg);
         print_usage(argv[0], stderr);
         return 1;
      }
   }

   if (infile == NULL || outfile == NULL || prefix == NULL) {
      fprintf(stderr, "Missing required argument.\n");
      print_usage(argv[0], stderr);
      return -1;
   }

   int fd = open(infile, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Failed to open %s\n", infile);
      ralloc_free(mem_ctx);
      return 1;
   }

   off_t spirv_len = lseek(fd, 0, SEEK_END);
   const void *spirv_map = mmap(NULL, spirv_len, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);
   if (spirv_map == MAP_FAILED) {
      fprintf(stderr, "Failed to mmap the file: errno=%d, %s\n", errno,
              strerror(errno));
      ralloc_free(mem_ctx);
      return 1;
   }

   FILE *fp = stdout;
   if (outfile != NULL)
      fp = fopen(outfile, "w");

   glsl_type_singleton_init_or_ref();

   fprintf(fp, "/*\n");
   fprintf(fp, " * Copyright The Asahi Linux Contributors\n");
   fprintf(fp, " * SPDX-License-Identifier: MIT\n");
   fprintf(fp, " *\n");
   fprintf(fp, " * Autogenerated file, do not edit\n");
   fprintf(fp, " */\n");
   fprintf(fp, " #include <stdint.h>\n");

   /* Compile SPIR-V to NIR */
   nir_shader *nir = compile(mem_ctx, spirv_map, spirv_len);

   {
      nir_builder b = nir_builder_init_simple_shader(
         MESA_SHADER_COMPUTE, &agx_nir_options, "Helper shader");

      nir_function *func =
         nir_shader_get_function_for_name(nir, "libagx_helper");

      nir_call(&b, nir_function_clone(b.shader, func));

      struct agx_shader_part compiled;
      struct agx_shader_key key = {
         .libagx = nir,
         .is_helper = true,
      };

      agx_preprocess_nir(b.shader, nir);
      agx_compile_shader_nir(b.shader, &key, NULL, &compiled);

      print_u32_data(fp, "libagx_g13", "helper", compiled.binary,
                     compiled.info.binary_size);
      free(compiled.binary);
      ralloc_free(b.shader);

      /* Remove the NIR function, it's compiled, we don't need it at runtime */
      exec_node_remove(&func->node);
   }

   spirv_library_to_nir_builder(fp, spirv_map, spirv_len / 4, &spirv_options);

   /* Serialize NIR for embedding */
   struct blob blob;
   blob_init(&blob);
   nir_serialize(&blob, nir, true /* strip */);
   print_u32_data(fp, prefix, "nir", (const uint32_t *)blob.data, blob.size);
   blob_finish(&blob);

   glsl_type_singleton_decref();

   if (fp != stdout)
      fclose(fp);

   ralloc_free(mem_ctx);
   return 0;
}
