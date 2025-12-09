/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "common/pvr_device_info.h"
#include "compiler/glsl_types.h"
#include "compiler/shader_enums.h"
#include "compiler/spirv/nir_spirv.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_builder_opcodes.h"
#include "nir/nir_intrinsics.h"
#include "nir/nir_precompiled.h"
#include "pco/pco.h"
#include "pco/pco_data.h"
#include "util/macros.h"

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define CLC_PREFIX "pco_usclib"

#define VS_PREFIX "vs_"
#define FS_PREFIX "fs_"
#define CS_PREFIX "cs_"
#define COMMON_SUFFIX "_common"
#define COMMON_DEVICE "common"

static const struct pvr_device_info pvr_device_info_common = {
   .ident =
      (struct pvr_device_ident){
         .device_id = 0,
         .series_name = COMMON_DEVICE,
         .public_name = COMMON_DEVICE,
      },

   .features =
      (struct pvr_device_features){
         .has_common_store_size_in_dwords = true,
         .has_compute = true,
         .has_ipf_creq_pf = true,
         .has_isp_max_tiles_in_flight = true,
         .has_isp_samples_per_pixel = true,
         .has_max_instances_per_pds_task = true,
         .has_max_multisample = true,
         .has_max_partitions = true,
         .has_max_usc_tasks = true,
         .has_num_clusters = true,
         .has_num_raster_pipes = true,
         .has_pbe2_in_xe = true,
         .has_pbe_filterable_f16 = true,
         .has_pbe_yuv = true,
         .has_roguexe = true,
         .has_screen_size8K = true,
         .has_simple_internal_parameter_format = true,
         .has_simple_internal_parameter_format_v2 = true,
         .has_simple_parameter_format_version = true,
         .has_slc_cache_line_size_bits = true,
         .has_tile_size_16x16 = true,
         .has_tile_size_x = true,
         .has_tile_size_y = true,
         .has_tpu_border_colour_enhanced = true,
         .has_tpu_extended_integer_lookup = true,
         .has_tpu_image_state_v2 = true,
         .has_unified_store_depth = true,
         .has_usc_f16sop_u8 = true,
         .has_usc_min_output_registers_per_pix = true,
         .has_usc_pixel_partition_mask = true,
         .has_usc_slots = true,
         .has_uvs_banks = true,
         .has_uvs_pba_entries = true,
         .has_uvs_vtx_entries = true,
         .has_vdm_cam_size = true,
         .has_vdm_degenerate_culling = true,

         .common_store_size_in_dwords = 512U * 4U * 4U,
         .isp_max_tiles_in_flight = 1U,
         .isp_samples_per_pixel = 1U,
         .max_instances_per_pds_task = 32U,
         .max_multisample = 4U,
         .max_partitions = 4U,
         .max_usc_tasks = 24U,
         .num_clusters = 1U,
         .num_raster_pipes = 1U,
         .simple_parameter_format_version = 2U,
         .slc_cache_line_size_bits = 512U,
         .tile_size_x = 16U,
         .tile_size_y = 16U,
         .unified_store_depth = 64U,
         .usc_min_output_registers_per_pix = 1U,
         .usc_slots = 14U,
         .uvs_banks = 2U,
         .uvs_pba_entries = 320U,
         .uvs_vtx_entries = 288U,
         .vdm_cam_size = 32U,

         .has_s8xe = true,
         .has_usc_itr_parallel_instances = true,

         .usc_itr_parallel_instances = 4U,
      },

   .enhancements = (struct pvr_device_enhancements){ 0 },
   .quirks = (struct pvr_device_quirks){ 0 },
};

/* Standard optimization loop */
static void optimize(nir_shader *nir)
{
   bool progress;
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_split_var_copies);
      NIR_PASS(progress, nir, nir_split_struct_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_lower_var_copies);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_lower_all_phis_to_scalar);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      nir_opt_peephole_select_options peep_opts = {
         .limit = 64,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peep_opts);
      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_lower_undef_to_zero);

      NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);

   } while (progress);
}

static nir_shader *
spv_to_nir(void *mem_ctx, uint32_t *spirv_map, unsigned spirv_len)
{
   static const struct spirv_to_nir_options precomp_spirv_options = {
      .environment = NIR_SPIRV_OPENCL,
      .shared_addr_format = nir_address_format_62bit_generic,
      .global_addr_format = nir_address_format_62bit_generic,
      .temp_addr_format = nir_address_format_62bit_generic,
      .constant_addr_format = nir_address_format_64bit_global,
      .create_library = true,
   };

   nir_shader *nir = spirv_to_nir(spirv_map,
                                  spirv_len / 4,
                                  NULL,
                                  0,
                                  MESA_SHADER_KERNEL,
                                  "library",
                                  &precomp_spirv_options,
                                  pco_nir_options());

   nir_validate_shader(nir, "after spirv_to_nir");
   nir_validate_ssa_dominance(nir, "after spirv_to_nir");
   ralloc_steal(mem_ctx, nir);

   nir_fixup_is_exported(nir);

   NIR_PASS(_, nir, nir_lower_system_values);
   NIR_PASS(_, nir, nir_lower_calls_to_builtins);

   nir_lower_compute_system_values_options cs = { .global_id_is_32bit = true };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &cs);

   /* We have to lower away local constant initializers right before we
    * inline functions.  That way they get properly initialized at the top
    * of the function and not at the top of its caller.
    */
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   nir_remove_non_exported(nir);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_deref);

   /* We can't deal with constant data, get rid of it */
   nir_lower_constant_to_temp(nir);

   /* We can go ahead and lower the rest of the constant initializers.  We do
    * this here so that nir_remove_dead_variables and
    * split_per_member_structs below see the corresponding stores.
    */
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);

   /* LLVM loves to take advantage of the fact that vec3s in OpenCL are 16B
    * aligned and so it can just read/write them as vec4s.  This results in a
    * LOT of vec4->vec3 casts on loads and stores.  One solution to this
    * problem is to get rid of all vec3 variables.
    */
   NIR_PASS(_,
            nir,
            nir_lower_vec3_to_vec4,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant);

   /* We assign explicit types early so that the optimizer can take advantage
    * of that information and hopefully get rid of some of our memcpys.
    */
   NIR_PASS(_,
            nir,
            nir_lower_vars_to_explicit_types,
            nir_var_uniform | nir_var_shader_temp | nir_var_function_temp |
               nir_var_mem_shared | nir_var_mem_global,
            glsl_get_cl_type_size_align);

   optimize(nir);

   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_all, NULL);

   /* Lower again, this time after dead-variables to get more compact
    * variable layouts.
    */
   NIR_PASS(_,
            nir,
            nir_lower_vars_to_explicit_types,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant,
            glsl_get_cl_type_size_align);
   assert(nir->constant_data_size == 0);

   NIR_PASS(_, nir, nir_lower_memcpy);

   NIR_PASS(_,
            nir,
            nir_lower_explicit_io,
            nir_var_mem_constant,
            nir_address_format_64bit_global);

   NIR_PASS(_,
            nir,
            nir_lower_explicit_io,
            nir_var_uniform,
            nir_address_format_32bit_offset_as_64bit);

   /* Note: we cannot lower explicit I/O here, because we need derefs intact
    * for function calls into the library to work.
    */

   NIR_PASS(_, nir, nir_lower_convert_alu_types, NULL);
   NIR_PASS(_, nir, nir_opt_if, 0);
   NIR_PASS(_, nir, nir_opt_idiv_const, 16);

   optimize(nir);

   return nir;
}

static nir_def *load_kernel_input(nir_builder *b,
                                  unsigned num_components,
                                  unsigned bit_size,
                                  unsigned base)
{
   return nir_load_preamble(b, num_components, bit_size, .base = base);
}

/**
 * Common function to build a NIR shader and export the binary.
 *
 * \param ctx PCO context.
 * \param nir NIR shader.
 * \param data Shader data.
 * \return The finalized PCO shader.
 */
static pco_shader *build_shader(pco_ctx *ctx, nir_shader *nir, pco_data *data)
{
   pco_preprocess_nir(ctx, nir);
   pco_lower_nir(ctx, nir, data);
   pco_postprocess_nir(ctx, nir, data);

   pco_shader *shader = pco_trans_nir(ctx, nir, data, NULL);
   pco_process_ir(ctx, shader);
   pco_encode_ir(ctx, shader);

   return shader;
}

static inline mesa_shader_stage get_shader_stage(const char *name)
{
   if (!strncmp(VS_PREFIX, name, strlen(VS_PREFIX)))
      return MESA_SHADER_VERTEX;

   if (!strncmp(FS_PREFIX, name, strlen(FS_PREFIX)))
      return MESA_SHADER_FRAGMENT;

   if (!strncmp(CS_PREFIX, name, strlen(CS_PREFIX)))
      return MESA_SHADER_COMPUTE;

   UNREACHABLE("");
}

static inline bool is_shader_common(const char *name)
{
   unsigned name_len = strlen(name);
   unsigned suffix_len = strlen(COMMON_SUFFIX);

   if (name_len < suffix_len)
      return false;

   return !strcmp(&name[name_len - suffix_len], COMMON_SUFFIX);
}

static const char *
remap_variant(nir_function *func, UNUSED unsigned variant, const char *target)
{
   return is_shader_common(func->name) ? COMMON_DEVICE : target;
}

int main(int argc, char *argv[argc])
{
   if (argc < 4) {
      fprintf(
         stderr,
         "Usage: %s <input SPIR-V> <output header> <output source> [device(s...)]\n",
         argv[0]);

      return 1;
   }

   void *mem_ctx = ralloc_context(NULL);

   char *spv_file = argv[1];
   char *hdr_file = argv[2];
   char *src_file = argv[3];

   unsigned num_devices = argc - 4;
   const char **devices =
      ralloc_array_size(mem_ctx, sizeof(*devices), num_devices + 1);
   char **device_names =
      ralloc_array_size(mem_ctx, sizeof(*device_names), num_devices + 1);
   uint64_t *device_ids =
      ralloc_array_size(mem_ctx, sizeof(*device_ids), num_devices + 1);
   struct pvr_device_info *device_infos =
      ralloc_array_size(mem_ctx, sizeof(*device_infos), num_devices + 1);

   for (unsigned d = 0; d < num_devices; ++d) {
      devices[d] = ralloc_strdup(devices, argv[4 + d]);
      device_names[d] = ralloc_strdup(device_names, devices[d]);
      for (unsigned u = 0; u < strlen(device_names[d]); ++u)
         if (device_names[d][u] == '-')
            device_names[d][u] = '_';

      pvr_device_info_init_public_name(&device_infos[d], devices[d]);
      device_ids[d] = pvr_get_packed_bvnc(&device_infos[d]);
   }

   /* Common device. */
   devices[num_devices] = ralloc_strdup(devices, COMMON_DEVICE);
   device_names[num_devices] = ralloc_strdup(device_names, COMMON_DEVICE);
   device_ids[num_devices] = 0U;
   memcpy(&device_infos[num_devices],
          &pvr_device_info_common,
          sizeof(pvr_device_info_common));
   ++num_devices;

   int fd = open(spv_file, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Failed to open %s\n", spv_file);
      goto err_free_mem_ctx;
   }

   off_t spirv_len = lseek(fd, 0, SEEK_END);
   assert(spirv_len % 4 == 0);

   void *spirv_map = mmap(NULL, spirv_len, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);

   if (spirv_map == MAP_FAILED) {
      fprintf(stderr,
              "Failed to mmap the file: errno=%d, %s\n",
              errno,
              strerror(errno));
      goto err_free_mem_ctx;
   }

   FILE *fp_hdr = fopen(hdr_file, "w");
   if (!fp_hdr) {
      fprintf(stderr, "Failed to open %s\n", hdr_file);
      goto err_unmap_spirv;
   }

   FILE *fp_src = fopen(src_file, "w");
   if (!fp_src) {
      fprintf(stderr, "Failed to open %s\n", src_file);
      goto err_close_hdr;
   }

   nir_precomp_print_header(fp_src,
                            fp_hdr,
                            "Imagination Technologies Ltd.",
                            basename(hdr_file));

   pco_ctx *ctx = pco_ctx_create(NULL, mem_ctx);
   nir_shader *nir = spv_to_nir(mem_ctx, spirv_map, spirv_len);
   struct nir_precomp_opts opts = { 0 };

   nir_precomp_print_target_enum_map(fp_src,
                                     fp_hdr,
                                     CLC_PREFIX,
                                     num_devices,
                                     (const char **)device_names,
                                     device_ids);

   nir_precomp_print_program_enum(fp_hdr, nir, CLC_PREFIX);
   nir_precomp_print_dispatch_macros(fp_hdr, &opts, nir);

   nir_foreach_entrypoint (func, nir) {
      unsigned num_variants = nir_precomp_nr_variants(func);

      nir_precomp_print_layout_struct(fp_hdr, &opts, func);

      mesa_shader_stage stage = get_shader_stage(func->name);
      bool is_common = is_shader_common(func->name);

      for (unsigned variant = 0; variant < num_variants; ++variant) {
         nir_shader *s = nir_precompiled_build_variant(func,
                                                       stage,
                                                       variant,
                                                       pco_nir_options(),
                                                       &opts,
                                                       load_kernel_input);

         nir_link_shader_functions(s, nir);
         NIR_PASS(_, s, nir_inline_functions);
         nir_remove_non_entrypoints(s);
         NIR_PASS(_, s, nir_opt_deref);
         NIR_PASS(_, s, nir_lower_vars_to_ssa);
         NIR_PASS(_, s, nir_remove_dead_derefs);
         NIR_PASS(_,
                  s,
                  nir_remove_dead_variables,
                  nir_var_function_temp | nir_var_shader_temp,
                  NULL);
         NIR_PASS(_,
                  s,
                  nir_lower_vars_to_explicit_types,
                  nir_var_shader_temp | nir_var_function_temp,
                  glsl_get_cl_type_size_align);

         NIR_PASS(_,
                  s,
                  nir_lower_vars_to_explicit_types,
                  nir_var_mem_shared,
                  glsl_get_cl_type_size_align);

         NIR_PASS(_,
                  s,
                  nir_lower_explicit_io,
                  nir_var_shader_temp | nir_var_function_temp |
                     nir_var_mem_shared | nir_var_mem_global,
                  nir_address_format_62bit_generic);

         /* Unroll loops before lowering indirects */
         bool progress;
         do {
            progress = false;
            NIR_PASS(progress, s, nir_opt_loop);
         } while (progress);

         for (unsigned d = 0; d < num_devices; ++d) {
            if (is_common && d != (num_devices - 1))
               continue;

            pco_ctx_update_dev_info(ctx, &device_infos[d]);

            nir_shader *clone = nir_shader_clone(NULL, s);

            pco_data data = { 0 };
            pco_shader *shader = build_shader(ctx, clone, &data);
            pco_precomp_data precomp_data = pco_get_precomp_data(shader);

            unsigned binary_size =
               pco_shader_binary_size(shader) + sizeof(precomp_data);
            assert(!(binary_size % sizeof(uint32_t)));

            uint32_t *binary_data = malloc(binary_size);
            memcpy(binary_data, &precomp_data, sizeof(precomp_data));
            memcpy((uint8_t *)binary_data + sizeof(precomp_data),
                   pco_shader_binary_data(shader),
                   pco_shader_binary_size(shader));

            nir_precomp_print_blob(fp_src,
                                   func->name,
                                   device_names[d],
                                   variant,
                                   binary_data,
                                   binary_size,
                                   true);

            free(binary_data);
            ralloc_free(shader);
            ralloc_free(clone);
         }

         ralloc_free(s);
      }
   }

   for (unsigned d = 0; d < num_devices; ++d) {
      nir_precomp_print_extern_binary_map(fp_hdr, CLC_PREFIX, device_names[d]);
      nir_precomp_print_binary_map(fp_src,
                                   nir,
                                   CLC_PREFIX,
                                   device_names[d],
                                   remap_variant);
   }

   nir_precomp_print_target_binary_map(fp_src,
                                       fp_hdr,
                                       CLC_PREFIX,
                                       num_devices,
                                       (const char **)device_names);

   /* Remove common shaders - no need to preserve their NIR. */
   nir_foreach_entrypoint_safe (func, nir) {
      if (!is_shader_common(func->name))
         continue;

      exec_node_remove(&func->node);
   }

   nir_precomp_print_nir(fp_src, fp_hdr, nir, CLC_PREFIX, "nir");

   ralloc_free(mem_ctx);
   munmap(spirv_map, spirv_len);
   fclose(fp_src);
   fclose(fp_hdr);

   return 0;

err_close_hdr:
   fclose(fp_hdr);

err_unmap_spirv:
   munmap(spirv_map, spirv_len);

err_free_mem_ctx:
   ralloc_free(mem_ctx);

   return 1;
}
