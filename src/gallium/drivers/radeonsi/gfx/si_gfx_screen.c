/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "si_gfx.h"
#include "si_pipe.h"
#include "compiler/nir/nir.h"
#include "ac_shader_util.h"
#include "ac_shadowed_regs.h"
#include "util/disk_cache.h"
#include "aco_interface.h"
#include "util/hex.h"
#include "util/u_cpu_detect.h"
#include "util/u_screen.h"

#include <sys/utsname.h>
#include <ctype.h>

#if AMD_LLVM_AVAILABLE
#include "ac_llvm_util.h"
#endif

#include <xf86drm.h>

static const struct debug_named_value radeonsi_shader_debug_options[] = {
   /* Shader logging options: */
   {"vs", DBG(VS), "Print vertex shaders"},
   {"ps", DBG(PS), "Print pixel shaders"},
   {"gs", DBG(GS), "Print geometry shaders"},
   {"tcs", DBG(TCS), "Print tessellation control shaders"},
   {"tes", DBG(TES), "Print tessellation evaluation shaders"},
   {"cs", DBG(CS), "Print compute shaders"},
   {"ts", DBG(TS), "Print task shaders"},
   {"ms", DBG(MS), "Print mesh shaders"},

   {"initnir", DBG(INIT_NIR), "Print initial input NIR when shaders are created"},
   {"nir", DBG(NIR), "Print final NIR after lowering when shader variants are created"},
   {"initllvm", DBG(INIT_LLVM), "Print initial LLVM IR before optimizations"},
   {"llvm", DBG(LLVM), "Print final LLVM IR"},
   {"initaco", DBG(INIT_ACO), "Print initial ACO IR before optimizations"},
   {"aco", DBG(ACO), "Print final ACO IR"},
   {"asm", DBG(ASM), "Print final shaders in asm"},
   {"stats", DBG(STATS), "Print shader-db stats to stderr"},

   /* Shader compiler options the shader cache should be aware of: */
   {"w32ge", DBG(W32_GE), "Use Wave32 for vertex, tessellation, and geometry shaders."},
   {"w32ps", DBG(W32_PS), "Use Wave32 for pixel shaders."},
   {"w32cs", DBG(W32_CS), "Use Wave32 for computes shaders."},
   {"w64ge", DBG(W64_GE), "Use Wave64 for vertex, tessellation, and geometry shaders."},
   {"w64ps", DBG(W64_PS), "Use Wave64 for pixel shaders."},
   {"w64cs", DBG(W64_CS), "Use Wave64 for computes shaders."},

   /* Shader compiler options (with no effect on the shader cache): */
   {"checkir", DBG(CHECK_IR), "Enable additional sanity checks on shader IR"},
   {"mono", DBG(MONOLITHIC_SHADERS), "Use old-style monolithic shaders compiled on demand"},
   {"nooptvariant", DBG(NO_OPT_VARIANT), "Disable compiling optimized shader variants."},
   {"usellvm", DBG(USE_LLVM), "Use LLVM as shader compiler when possible"},

   DEBUG_NAMED_VALUE_END /* must be last */
};

static void si_init_gs_info(struct si_screen *sscreen)
{
   sscreen->gs_table_depth = ac_get_gs_table_depth(sscreen->info.gfx_level, sscreen->info.family);
}

static void
parse_hex(char *out, const char *in, unsigned length)
{
   for (unsigned i = 0; i < length; ++i)
      out[i] = 0;

   for (unsigned i = 0; i < 2 * length; ++i) {
      unsigned v = in[i] <= '9' ? in[i] - '0' : (in[i] >= 'a' ? (in[i] - 'a' + 10) : (in[i] - 'A' + 10));
      out[i / 2] |= v << (4 * (1 - i % 2));
   }
}

static void si_disk_cache_create(struct si_screen *sscreen)
{
   /* Don't use the cache if shader dumping is enabled. */
   if (sscreen->shader_debug_flags & DBG_ALL_SHADERS)
      return;

   blake3_hasher ctx;
   unsigned char blake3[BLAKE3_KEY_LEN];
   char cache_id[BLAKE3_HEX_LEN];

   _mesa_blake3_init(&ctx);

#ifdef RADEONSI_BUILD_ID_OVERRIDE
   {
      unsigned size = strlen(RADEONSI_BUILD_ID_OVERRIDE) / 2;
      char *data = alloca(size);
      parse_hex(data, RADEONSI_BUILD_ID_OVERRIDE, size);
      _mesa_blake3_update(&ctx, data, size);
   }
#else
   if (!disk_cache_get_function_identifier(si_disk_cache_create, &ctx))
      return;
#endif

#if AMD_LLVM_AVAILABLE
   if (!disk_cache_get_function_identifier(LLVMInitializeAMDGPUTargetInfo, &ctx))
      return;
#endif

   /* NIR options depend on si_screen::use_aco, which affects all shaders, including GLSL
    * compilation.
    */
   _mesa_blake3_update(&ctx, &sscreen->use_aco, sizeof(sscreen->use_aco));

   _mesa_blake3_final(&ctx, blake3);
   mesa_bytes_to_hex(cache_id, blake3, BLAKE3_KEY_LEN);

   sscreen->disk_shader_cache = disk_cache_create(ac_get_family_name(sscreen->info.family),
                                                  cache_id, sscreen->info.address32_hi);
}

static void si_set_max_shader_compiler_threads(struct pipe_screen *screen, unsigned max_threads)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   /* This function doesn't allow a greater number of threads than
    * the queue had at its creation. */
   util_queue_adjust_num_threads(&sscreen->shader_compiler_queue, max_threads, false);
   /* Don't change the number of threads on the low priority queue. */
}

static bool si_is_parallel_shader_compilation_finished(struct pipe_screen *screen, void *shader,
                                                       mesa_shader_stage shader_type)
{
   struct si_shader_selector *sel = (struct si_shader_selector *)shader;

   return util_queue_fence_is_signalled(&sel->ready);
}

static void si_setup_force_shader_use_aco(struct si_screen *sscreen, bool support_aco)
{
   /* Usage:
    *   1. shader type: vs|tcs|tes|gs|ps|cs, specify a class of shaders to use aco
    *   2. shader blake: specify a single shader blake directly to use aco
    *   3. filename: specify a file which contains shader blakes in lines
    */

   sscreen->use_aco_shader_type = MESA_SHADER_NONE;

   if (sscreen->use_aco || !support_aco)
      return;

   const char *option = debug_get_option("AMD_FORCE_SHADER_USE_ACO", NULL);
   if (!option)
      return;

   if (!strcmp("vs", option)) {
      sscreen->use_aco_shader_type = MESA_SHADER_VERTEX;
      return;
   } else if (!strcmp("tcs", option)) {
      sscreen->use_aco_shader_type = MESA_SHADER_TESS_CTRL;
      return;
   } else if (!strcmp("tes", option)) {
      sscreen->use_aco_shader_type = MESA_SHADER_TESS_EVAL;
      return;
   } else if (!strcmp("gs", option)) {
      sscreen->use_aco_shader_type = MESA_SHADER_GEOMETRY;
      return;
   } else if (!strcmp("ps", option)) {
      sscreen->use_aco_shader_type = MESA_SHADER_FRAGMENT;
      return;
   } else if (!strcmp("cs", option)) {
      sscreen->use_aco_shader_type = MESA_SHADER_COMPUTE;
      return;
   }

   blake3_hash blake;
   if (_mesa_blake3_from_printed_string(blake, option)) {
      sscreen->use_aco_shader_blakes = MALLOC(sizeof(blake));
      memcpy(sscreen->use_aco_shader_blakes[0], blake, sizeof(blake));
      sscreen->num_use_aco_shader_blakes = 1;
      return;
   }

   FILE *f = fopen(option, "r");
   if (!f) {
      mesa_loge("invalid AMD_FORCE_SHADER_USE_ACO value");
      return;
   }

   unsigned max_size = 16 * sizeof(blake3_hash);
   sscreen->use_aco_shader_blakes = MALLOC(max_size);

   char line[1024];
   while (fgets(line, sizeof(line), f)) {
      if (sscreen->num_use_aco_shader_blakes * sizeof(blake3_hash) >= max_size) {
         sscreen->use_aco_shader_blakes = REALLOC(
            sscreen->use_aco_shader_blakes, max_size, max_size * 2);
         max_size *= 2;
      }

      if (line[BLAKE3_PRINTED_LEN] == '\n')
         line[BLAKE3_PRINTED_LEN] = 0;

      if (_mesa_blake3_from_printed_string(
             sscreen->use_aco_shader_blakes[sscreen->num_use_aco_shader_blakes], line))
         sscreen->num_use_aco_shader_blakes++;
   }

   fclose(f);
}

static bool
is_pro_graphics(struct si_screen *sscreen)
{
   return  strstr(sscreen->info.marketing_name, "Pro") ||
           strstr(sscreen->info.marketing_name, "PRO") ||
           strstr(sscreen->info.marketing_name, "Frontier");
}

static bool
si_is_compute_copy_faster(struct pipe_screen *pscreen,
                          enum pipe_format src_format,
                          enum pipe_format dst_format,
                          unsigned width,
                          unsigned height,
                          unsigned depth,
                          bool cpu)
{
   if (cpu)
      /* very basic for now */
      return (uint64_t)width * height * depth > 64 * 64;
   return false;
}

static void
si_driver_thread_add_job(struct pipe_screen *screen, void *data,
                         struct util_queue_fence *fence,
                         pipe_driver_thread_func execute,
                         pipe_driver_thread_func cleanup,
                         const size_t job_size)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   util_queue_add_job(&sscreen->shader_compiler_queue, data, fence, execute, cleanup, job_size);
}

static struct disk_cache *si_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   return sscreen->disk_shader_cache;
}

static unsigned si_varying_expression_max_cost(nir_shader *producer, nir_shader *consumer)
{
   unsigned num_profiles = si_get_num_shader_profiles();

   for (unsigned i = 0; i < num_profiles; i++) {
      if (_mesa_printed_blake3_equal(consumer->info.source_blake3, si_shader_profiles[i].blake3)) {
         if (si_shader_profiles[i].options & SI_PROFILE_NO_OPT_UNIFORM_VARYINGS)
            return 0; /* only propagate constants */
         break;
      }
   }

   return ac_nir_varying_expression_max_cost(producer, consumer);
}

static bool enable_mesh_shader(struct si_screen *sscreen)
{
   return sscreen->use_ngg &&
      sscreen->info.gfx_level >= GFX10_3 &&
      /* TODO: not support user queue for now */
      !(sscreen->info.userq_ip_mask & BITFIELD_BIT(AMD_IP_GFX)) &&
      /* don't support LLVM */
      aco_is_gpu_supported(&sscreen->info) &&
      !(sscreen->debug_flags & DBG(USE_LLVM));
}

static bool si_alu_to_scalar_packed_math_filter(const nir_instr *instr, const void *data)
{
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      if (alu->def.bit_size == 16 && alu->def.num_components == 2 &&
          ac_nir_op_supports_packed_math_16bit(alu)) {
         /* ACO requires that all but the first bit of swizzle must be equal. */
         for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
            if ((alu->src[i].swizzle[0] >> 1) != (alu->src[i].swizzle[1] >> 1))
               return true;
         }
         return false;
      }
   }

   return true;
}

static void si_init_screen_nir_options(struct si_screen *sscreen)
{
   /*        |---------------------------------- Performance & Availability --------------------------------|
    *        |MAD/MAC/MADAK/MADMK|MAD_LEGACY|MAC_LEGACY|    FMA     |FMAC/FMAAK/FMAMK|FMA_LEGACY|PK_FMA_F16,|Best choice
    * Arch   |    F32,F16,F64    | F32,F16  | F32,F16  |F32,F16,F64 |    F32,F16     |   F32    |PK_FMAC_F16|F16,F32,F64
    * ------------------------------------------------------------------------------------------------------------------
    * gfx6,7 |     1 , - , -     |  1 , -   |  1 , -   |1/4, - ,1/16|     - , -      |    -     |   - , -   | - ,MAD,FMA
    * gfx8   |     1 , 1 , -     |  1 , -   |  - , -   |1/4, 1 ,1/16|     - , -      |    -     |   - , -   |MAD,MAD,FMA
    * gfx9   |     1 ,1|0, -     |  1 , -   |  - , -   | 1 , 1 ,1/16|    0|1, -      |    -     |   2 , -   |FMA,MAD,FMA
    * gfx10  |     1 , - , -     |  1 , -   |  1 , -   | 1 , 1 ,1/16|     1 , 1      |    -     |   2 , 2   |FMA,MAD,FMA
    * gfx10.3|     - , - , -     |  - , -   |  - , -   | 1 , 1 ,1/16|     1 , 1      |    1     |   2 , 2   |  all FMA
    * gfx11  |     - , - , -     |  - , -   |  - , -   | 2 , 2 ,1/16|     2 , 2      |    2     |   2 , 2   |  all FMA
    *
    * Tahiti, Hawaii, Carrizo, Vega20: FMA_F32 is full rate, FMA_F64 is 1/4
    * gfx9 supports MAD_F16 only on Vega10, Raven, Raven2, Renoir.
    * gfx9 supports FMAC_F32 only on Vega20, but doesn't support FMAAK and FMAMK.
    *
    * gfx8 prefers MAD for F16 because of MAC/MADAK/MADMK.
    * gfx9 and newer prefer FMA for F16 because of the packed instruction.
    * gfx10 and older prefer MAD for F32 because of the legacy instruction.
    */
   bool use_fma32 =
      sscreen->info.gfx_level >= GFX10_3 ||
      (sscreen->info.family >= CHIP_GFX940 && !sscreen->info.has_graphics) ||
      /* fma32 is too slow for gpu < gfx9, so apply the option only for gpu >= gfx9 */
      (sscreen->info.gfx_level >= GFX9 && sscreen->options.force_use_fma32);
   /* GFX8 has precision issues with 16-bit PS outputs. */
   bool has_16bit_io = sscreen->info.gfx_level >= GFX9;

   nir_shader_compiler_options *options = sscreen->nir_options;
   ac_nir_set_options(&sscreen->info.compiler_info, !sscreen->use_aco, options);

   options->lower_ffma16 = sscreen->info.gfx_level < GFX9;
   options->lower_ffma32 = !use_fma32;
   options->lower_ffma64 = false;
   options->fuse_ffma16 = sscreen->info.gfx_level >= GFX9;
   options->fuse_ffma32 = use_fma32;
   options->fuse_ffma64 = true;
   options->lower_uniforms_to_ubo = true;
   options->lower_to_scalar = true;
   options->lower_to_scalar_filter =
      sscreen->info.compiler_info.has_packed_math_16bit ? si_alu_to_scalar_packed_math_filter : NULL;
   options->max_unroll_iterations = 128;
   options->max_unroll_iterations_aggressive = 128;
   /* For OpenGL, rounding mode is undefined. We want fast packing with v_cvt_pkrtz_f16,
    * but if we use it, all f32->f16 conversions have to round towards zero,
    * because both scalar and vec2 down-conversions have to round equally.
    *
    * For OpenCL, rounding mode is explicit. This will only lower f2f16 to f2f16_rtz
    * when execution mode is rtz instead of rtne.
    *
    * GFX8 has precision issues with this option.
    */
   options->force_f2f16_rtz = sscreen->info.gfx_level >= GFX9;
   options->io_options |= (!has_16bit_io ? nir_io_mediump_is_32bit : 0) | nir_io_has_intrinsics |
                          (sscreen->use_ngg_culling ?
                              nir_io_compaction_groups_tes_inputs_into_pos_and_var_groups : 0);
   if (has_16bit_io) {
      options->lower_mediump_io = sscreen->options.mediump ? si_nir_lower_mediump_io_option
                                                           : si_nir_lower_mediump_io_default;
   }

   /* HW supports indirect indexing for: | Enabled in driver
    * -------------------------------------------------------
    * TCS inputs                         | Yes
    * TES inputs                         | Yes
    * GS inputs                          | No
    * -------------------------------------------------------
    * VS outputs before TCS              | No
    * TCS outputs                        | Yes
    * VS/TES outputs before GS           | No
    */
   options->varying_expression_max_cost = si_varying_expression_max_cost;

   unsigned max_support_shader = enable_mesh_shader(sscreen) ?
      MESA_SHADER_MESH : MESA_SHADER_COMPUTE;
   for (unsigned i = 0; i <= max_support_shader; i++)
      sscreen->b.nir_options[i] = sscreen->nir_options;
}

static void si_init_shader_caps(struct si_screen *sscreen)
{
   for (unsigned i = 0; i <= MESA_SHADER_MESH; i++) {
      if (!sscreen->b.nir_options[i])
         continue;

      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&sscreen->b.shader_caps[i];

      /* Shader limits. */
      caps->max_instructions =
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections =
      caps->max_control_flow_depth = 16384;
      caps->max_inputs = i == MESA_SHADER_VERTEX ? SI_MAX_ATTRIBS : 32;
      caps->max_outputs = i == MESA_SHADER_FRAGMENT ? 8 : 32;
      caps->max_temps = 256; /* Max native temporaries. */
      caps->max_const_buffer0_size = 1 << 26; /* 64 MB */
      caps->max_const_buffers = SI_NUM_CONST_BUFFERS;
      caps->max_texture_samplers =
      caps->max_sampler_views = SI_NUM_SAMPLERS;
      caps->max_shader_buffers = SI_NUM_SHADER_BUFFERS;
      caps->max_shader_images = SI_NUM_IMAGES;

      caps->supported_irs = (1 << PIPE_SHADER_IR_TGSI) | (1 << PIPE_SHADER_IR_NIR);

      /* Supported boolean features. */
      caps->cont_supported = true;
      caps->tgsi_sqrt_supported = true;
      caps->indirect_temp_addr = true;
      caps->indirect_const_addr = true;
      caps->integers = true;
      caps->int64_atomics = true;
      caps->tgsi_any_inout_decl_range = true;

      /* We need F16C for fast FP16 conversions in glUniform.
       * It's supported since Intel Ivy Bridge and AMD Bulldozer.
       */
      bool has_16bit_alu = sscreen->info.gfx_level >= GFX8 && util_get_cpu_caps()->has_f16c;

      caps->fp16 = has_16bit_alu;
      caps->fp16_derivatives = has_16bit_alu;
      caps->fp16_const_buffers = has_16bit_alu;
      caps->int16 = has_16bit_alu;
      caps->glsl_16bit_consts = has_16bit_alu;
      caps->glsl_16bit_load_dst = sscreen->info.gfx_level >= GFX9;
   }
}

static void si_init_compute_caps(struct si_screen *sscreen)
{
   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&sscreen->b.compute_caps;

   caps->grid_dimension = 3;

   /* Use this size, so that internal counters don't overflow 64 bits. */
   caps->max_grid_size[0] = UINT32_MAX;
   caps->max_grid_size[1] = UINT16_MAX;
   caps->max_grid_size[2] = UINT16_MAX;

   caps->max_block_size[0] =
   caps->max_block_size[1] =
   caps->max_block_size[2] = 1024;

   caps->max_threads_per_block = 1024;
   caps->address_bits = 64;

   /* Return 1/4 of the heap size as the maximum because the max size is not practically
    * allocatable.
    */
   caps->max_mem_alloc_size = (sscreen->info.max_heap_size_kb / 4) * 1024ull;

   /* In OpenCL, the MAX_MEM_ALLOC_SIZE must be at least
    * 1/4 of the MAX_GLOBAL_SIZE.  Since the
    * MAX_MEM_ALLOC_SIZE is fixed for older kernels,
    * make sure we never report more than
    * 4 * MAX_MEM_ALLOC_SIZE.
    */
   caps->max_global_size = MIN2(4 * caps->max_mem_alloc_size,
                                sscreen->info.max_heap_size_kb * 1024ull);

   /* Value reported by the closed source driver. */
   caps->max_local_size = sscreen->info.gfx_level == GFX6 ? 32 * 1024 : 64 * 1024;

   caps->max_clock_frequency = sscreen->info.max_gpu_freq_mhz;
   caps->max_compute_units = sscreen->info.num_cu;

   unsigned threads = 1024;
   unsigned subgroup_size =
      sscreen->shader_debug_flags & DBG(W64_CS) || sscreen->info.gfx_level < GFX10 ? 64 : 32;
   caps->max_subgroups = threads / subgroup_size;

   if (sscreen->shader_debug_flags & DBG(W32_CS))
      caps->subgroup_sizes = 32;
   else if (sscreen->shader_debug_flags & DBG(W64_CS))
      caps->subgroup_sizes = 64;
   else
      caps->subgroup_sizes = sscreen->info.gfx_level < GFX10 ? 64 : 64 | 32;

   caps->max_variable_threads_per_block =
      sscreen->info.compiler_info.has_cs_regalloc_hang_bug ? 256 : SI_MAX_VARIABLE_THREADS_PER_BLOCK;
}

static void si_init_mesh_caps(struct si_screen *sscreen)
{
   struct pipe_mesh_caps *caps = (struct pipe_mesh_caps *)&sscreen->b.caps.mesh;

   caps->max_task_work_group_total_count = 1 << 22;
   caps->max_mesh_work_group_total_count = 1 << 22;
   caps->max_mesh_work_group_invocations = 256;
   caps->max_task_work_group_invocations = 1024;
   caps->max_task_payload_size = 16384;
   caps->max_task_shared_memory_size = 65536;
   caps->max_mesh_shared_memory_size = 28672;
   caps->max_task_payload_and_shared_memory_size = 65536;
   caps->max_mesh_payload_and_shared_memory_size =
      caps->max_task_payload_size + caps->max_mesh_shared_memory_size;
   caps->max_mesh_output_memory_size = 32 * 1024;
   caps->max_mesh_payload_and_output_memory_size =
      caps->max_task_payload_size + caps->max_mesh_output_memory_size;
   caps->max_mesh_output_vertices = 256;
   caps->max_mesh_output_primitives = 256;
   caps->max_mesh_output_components = 128;
   caps->max_mesh_output_layers = 8;
   caps->max_mesh_multiview_view_count = 1;
   caps->mesh_output_per_vertex_granularity = 1;
   caps->mesh_output_per_primitive_granularity = 1;

   caps->max_preferred_task_work_group_invocations = 64;
   caps->max_preferred_mesh_work_group_invocations = 128;
   caps->mesh_prefers_local_invocation_vertex_output = true;
   caps->mesh_prefers_local_invocation_primitive_output = true;
   caps->mesh_prefers_compact_vertex_output = true;
   caps->mesh_prefers_compact_primitive_output = false;

   caps->max_task_work_group_count[0] =
   caps->max_task_work_group_count[1] =
   caps->max_task_work_group_count[2] = 65535;

   caps->max_mesh_work_group_count[0] =
   caps->max_mesh_work_group_count[1] =
   caps->max_mesh_work_group_count[2] = 65535;

   caps->max_task_work_group_size[0] =
   caps->max_task_work_group_size[1] =
   caps->max_task_work_group_size[2] = 1024;

   caps->max_mesh_work_group_size[0] =
   caps->max_mesh_work_group_size[1] =
   caps->max_mesh_work_group_size[2] = 256;

   caps->pipeline_statistic_queries = sscreen->info.gfx_level >= GFX11;
}

static void si_init_gfx_caps(struct si_screen *sscreen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&sscreen->b.caps;

   /* Gfx8 (Polaris11) hangs, so don't enable this on Gfx8 and older chips. */
   bool enable_sparse =
      sscreen->info.gfx_level >= GFX9 && sscreen->info.has_sparse;

   /* Supported features (boolean caps). */
   caps->max_dual_source_render_targets = true;
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->texture_mirror_clamp = true;
   caps->texture_shadow_lod = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->blend_equation_separate = true;
   caps->texture_swizzle = true;
   caps->depth_clip_disable = true;
   caps->depth_clip_disable_separate = true;
   caps->shader_stencil_export = true;
   caps->vertex_element_instance_divisor = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->vertex_color_unclamped = true;
   caps->start_instance = true;
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->vertex_color_clamped = true;
   caps->fragment_color_clamped = true;
   caps->vs_instanceid = true;
   caps->texture_buffer_objects = true;
   caps->vs_layer_viewport = true;
   caps->query_pipeline_statistics = true;
   caps->sample_shading = true;
   caps->draw_indirect = true;
   caps->clip_halfz = true;
   caps->vs_window_space_position = true;
   caps->polygon_offset_clamp = true;
   caps->multisample_z_resolve = true;
   caps->quads_follow_provoking_vertex_convention = true;
   caps->tgsi_texcoord = true;
   caps->fs_fine_derivative = true;
   caps->conditional_render_inverted = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->depth_bounds_test = true;
   caps->sampler_view_target = true;
   caps->texture_query_lod = true;
   caps->texture_gather_sm5 = true;
   caps->texture_query_samples = true;
   caps->force_persample_interp = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true;
   caps->invalidate_buffer = true;
   caps->surface_reinterpret_blocks = true;
   caps->compressed_surface_reinterpret_blocks_layered = true;
   caps->query_buffer_object = true;
   caps->query_memory_info = true;
   caps->shader_pack_half_float = true;
   caps->framebuffer_no_attachment = true;
   caps->robust_buffer_access_behavior = true;
   caps->string_marker = true;
   caps->cull_distance = true;
   caps->shader_array_components = true;
   caps->stream_output_pause_resume = true;
   caps->stream_output_interleave_buffers = true;
   caps->doubles = true;
   caps->tes_layer_viewport = true;
   caps->bindless_texture = true;
   caps->query_timestamp = true;
   caps->query_time_elapsed = true;
   caps->nir_samplers_as_deref = true;
   caps->memobj = true;
   caps->load_constbuf = true;
   caps->int64 = true;
   caps->shader_clock = true;
   caps->can_bind_const_buffer_as_vertex = true;
   caps->allow_mapped_buffers_during_execution = true;
   caps->signed_vertex_buffer_offset = true;
   caps->shader_ballot = true;
   caps->shader_group_vote = true;
   caps->compute_grid_info_last_block = true;
   caps->image_load_formatted = true;
   caps->prefer_compute_for_multimedia = true;
   caps->packed_uniforms = true;
   caps->gl_spirv = true;
   caps->alpha_to_coverage_dither_control = true;
   caps->map_unsynchronized_thread_safe = true;
   caps->no_clip_on_copy_tex = true;
   caps->shader_atomic_int64 = true;
   caps->frontend_noop = true;
   caps->demote_to_helper_invocation = true;
   caps->prefer_real_buffer_in_constbuf0 = true;
   caps->compute_shader_derivatives = true;
   caps->image_atomic_inc_wrap = true;
   caps->image_store_formatted = true;
   caps->allow_draw_out_of_order = true;
   caps->query_so_overflow = true;
   caps->glsl_tess_levels_as_inputs = true;
   caps->device_reset_status_query = true;
   caps->texture_multisample = true;
   caps->allow_glthread_buffer_subdata_opt = true; /* TODO: remove if it's slow */
   caps->null_textures = true;
   caps->has_const_bw = true;
   caps->cl_gl_sharing = true;
   caps->call_finalize_nir_in_linker = true;
   caps->blit_3d = true;
   caps->glsl_bindless_handles_are_32bit = true;
   caps->fbfetch = 1;

   caps->graphics = sscreen->info.has_graphics;
   caps->mesh_shader = sscreen->b.nir_options[MESA_SHADER_MESH];
   caps->compute = sscreen->has_gfx_compute;

   /* Tahiti and Verde only: reduction mode is unsupported due to a bug
    * (it might work sometimes, but that's not enough)
    */
   caps->sampler_reduction_minmax =
   caps->sampler_reduction_minmax_arb =
      !(sscreen->info.family == CHIP_TAHITI || sscreen->info.family == CHIP_VERDE);

   caps->texture_transfer_modes =
      PIPE_TEXTURE_TRANSFER_BLIT | PIPE_TEXTURE_TRANSFER_COMPUTE;

   caps->draw_vertex_state = !(sscreen->debug_flags & DBG(NO_FAST_DISPLAY_LIST));

   caps->shader_samples_identical =
      sscreen->info.compiler_info.has_fmask && !(sscreen->debug_flags & DBG(NO_FMASK));

   caps->glsl_zero_init = 2;

   caps->generate_mipmap =
   caps->seamless_cube_map =
   caps->seamless_cube_map_per_texture =
   caps->cube_map_array =
      sscreen->info.compiler_info.has_3d_cube_border_color_mipmap;

   caps->post_depth_coverage = sscreen->info.gfx_level >= GFX10;

   caps->max_vertex_buffers = SI_MAX_ATTRIBS;

   caps->constant_buffer_offset_alignment =
   caps->texture_buffer_offset_alignment =
   caps->max_texture_gather_components =
   caps->max_stream_output_buffers =
   caps->max_vertex_streams =
   caps->shader_buffer_offset_alignment =
   caps->max_window_rectangles = 4;

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 460;

   /* Optimal number for good TexSubImage performance on Polaris10. */
   caps->max_texture_upload_memory_budget = 64 * 1024 * 1024;

   caps->gl_begin_end_buffer_size = 4096 * 1024;

   /* Return 1/4th of the heap size as the maximum because the max size is not practically
    * allocatable. Also, this can only return UINT32_MAX at most.
    */
   unsigned max_size = MIN2((sscreen->info.max_heap_size_kb * 1024ull) / 4, UINT32_MAX);

   /* Allow max 512 MB to pass CTS with a 32-bit build. */
   if (sizeof(void*) == 4)
      max_size = MIN2(max_size, 512 * 1024 * 1024);

   caps->max_constant_buffer_size =
   caps->max_shader_buffer_size = max_size;

   unsigned max_texels = caps->max_shader_buffer_size;

   /* FYI, BUF_RSRC_WORD2.NUM_RECORDS field limit is UINT32_MAX. */

   /* Gfx8 and older use the size in bytes for bounds checking, and the max element size
    * is 16B. Gfx9 and newer use the VGPR index for bounds checking.
    */
   if (sscreen->info.gfx_level <= GFX8)
      max_texels = MIN2(max_texels, UINT32_MAX / 16);
   else
      /* Gallium has a limitation that it can only bind UINT32_MAX bytes, not texels.
       * TODO: Remove this after the gallium interface is changed. */
      max_texels = MIN2(max_texels, UINT32_MAX / 16);

   caps->max_texel_buffer_elements = max_texels;

   /* Allow 1/4th of the heap size. */
   caps->max_texture_mb = sscreen->info.max_heap_size_kb / 1024 / 4;

   caps->prefer_back_buffer_reuse = false;
   caps->prefer_imm_arrays_as_constbuf = false;

   caps->performance_monitor =
      sscreen->info.gfx_level >= GFX7 && sscreen->info.gfx_level <= GFX10_3;

   caps->sparse_buffer_page_size = enable_sparse ? RADEON_SPARSE_PAGE_SIZE : 0;

   caps->constbuf0_flags = SI_RESOURCE_FLAG_32BIT;

   caps->draw_parameters =
   caps->multi_draw_indirect =
   caps->multi_draw_indirect_params = sscreen->has_draw_indirect_multi;

   caps->max_shader_patch_varyings = 30;

   caps->max_varyings =
   caps->max_gs_invocations = 32;

   caps->texture_border_color_quirk =
      sscreen->info.gfx_level <= GFX8 ? PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600 : 0;

   /* Stream output. */
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = 32 * 4;

   /* gfx9 has to report 256 to make piglit/gs-max-output pass.
    * gfx8 and earlier can do 1024.
    */
   caps->max_geometry_output_vertices = 256;
   caps->max_geometry_total_output_components = 4095;

   caps->max_vertex_attrib_stride = 2048;

   caps->max_texture_2d_size = sscreen->info.gfx_level >= GFX12 ? 65536 : 16384;
   caps->max_texture_cube_levels = sscreen->info.compiler_info.has_3d_cube_border_color_mipmap ?
      (sscreen->info.gfx_level >= GFX12 ? 17 : 15) /* 64K : 16K */ : 0;
   caps->max_texture_3d_levels = sscreen->info.compiler_info.has_3d_cube_border_color_mipmap ?
      /* This is limited by maximums that both the texture unit and layered rendering support. */
      (sscreen->info.gfx_level >= GFX12 ? 15 : /* 16K */
       (sscreen->info.gfx_level >= GFX10 ? 14 : 12)) /* 8K : 2K */ : 0;
   /* This is limited by maximums that both the texture unit and layered rendering support. */
   caps->max_texture_array_layers = sscreen->info.gfx_level >= GFX10 ? 8192 : 2048;

   /* Sparse texture */
   caps->max_sparse_texture_size = enable_sparse ? caps->max_texture_2d_size : 0;
   caps->max_sparse_3d_texture_size = enable_sparse ? (1 << (caps->max_texture_3d_levels - 1)) : 0;
   caps->max_sparse_array_texture_layers = enable_sparse ? caps->max_texture_array_layers : 0;
   caps->sparse_texture_full_array_cube_mipmaps =
   caps->query_sparse_texture_residency =
   caps->clamp_sparse_texture_lod = enable_sparse;

   /* Viewports and render targets. */
   caps->max_viewports = SI_MAX_VIEWPORTS;
   caps->viewport_subpixel_bits =
   caps->rasterizer_subpixel_bits =
   caps->max_render_targets = 8;
   caps->framebuffer_msaa_constraints = sscreen->info.has_eqaa_surface_allocator ? 2 : 0;

   caps->min_texture_gather_offset =
   caps->min_texel_offset = -32;

   caps->max_texture_gather_offset =
   caps->max_texel_offset = 31;

   caps->shader_subgroup_size = 64;
   caps->shader_subgroup_supported_stages =
      BITFIELD_MASK(caps->mesh_shader ? MESA_SHADER_MESH_STAGES : MESA_SHADER_STAGES);
   caps->shader_subgroup_supported_features = PIPE_SHADER_SUBGROUP_FEATURE_MASK;
   caps->shader_subgroup_quad_all_stages = true;

   caps->min_line_width =
   caps->min_line_width_aa = 1; /* due to axis-aligned end caps at line width 1 */

   caps->min_point_size =
   caps->min_point_size_aa =
   caps->point_size_granularity =
   caps->line_width_granularity = 1.0 / 8.0; /* due to the register field precision */

   /* This depends on the quant mode, though the precise interactions are unknown. */
   caps->max_line_width =
   caps->max_line_width_aa = 2048;

   caps->max_point_size =
   caps->max_point_size_aa = SI_MAX_POINT_SIZE;

   caps->max_texture_anisotropy = 16.0f;

   /* The hw can do 31, but this test fails if we use that:
    *    KHR-GL46.texture_lod_bias.texture_lod_bias_all
    */
   caps->max_texture_lod_bias = 16;

   /* Override the value set by u_init_pipe_screen_caps because it was called
    * before shader caps are set.
    */
   caps->hardware_gl_select = debug_get_bool_option("MESA_HW_ACCEL_SELECT", true);
}

bool si_init_gfx_screen(struct si_screen *sscreen) {
   unsigned hw_threads, num_comp_hi_threads, num_comp_lo_threads;
   const bool support_aco = aco_is_gpu_supported(&sscreen->info);
   bool support_llvm = false;

#if AMD_LLVM_AVAILABLE
   support_llvm = strlen(ac_get_llvm_processor_name(sscreen->info.family)) != 0;
#endif

   sscreen->has_gfx_compute = support_aco || support_llvm;

   if (!sscreen->has_gfx_compute)
      return true;

   ac_get_task_info(&sscreen->info, &sscreen->task_info);

   si_disk_cache_create(sscreen);

   if (sscreen->info.gfx_level >= GFX11) {
      sscreen->use_ngg = true;
      sscreen->use_ngg_culling = sscreen->info.max_render_backends >= 2 &&
                                 !(sscreen->debug_flags & DBG(NO_NGG_CULLING));
   } else {
      sscreen->use_ngg = !(sscreen->debug_flags & DBG(NO_NGG)) &&
                         sscreen->info.gfx_level >= GFX10 &&
                         (sscreen->info.family != CHIP_NAVI14 ||
                          is_pro_graphics(sscreen));
      sscreen->use_ngg_culling = sscreen->use_ngg &&
                                 sscreen->info.max_render_backends >= 2 &&
                                 !(sscreen->debug_flags & DBG(NO_NGG_CULLING));
   }

   sscreen->has_draw_indirect_multi =
      (sscreen->info.family >= CHIP_POLARIS10) ||
      (sscreen->info.gfx_level == GFX8 && sscreen->info.pfp_fw_version >= 121 &&
       sscreen->info.me_fw_version >= 87) ||
      (sscreen->info.gfx_level == GFX7 && sscreen->info.pfp_fw_version >= 211 &&
       sscreen->info.me_fw_version >= 173) ||
      (sscreen->info.gfx_level == GFX6 && sscreen->info.pfp_fw_version >= 79 &&
       sscreen->info.me_fw_version >= 142);

   si_driver_ds_init();

   sscreen->b.get_disk_shader_cache = si_get_disk_shader_cache;
   sscreen->b.is_compute_copy_faster = si_is_compute_copy_faster;
   sscreen->b.driver_thread_add_job = si_driver_thread_add_job;

   sscreen->context_roll_log_filename = debug_get_option("AMD_ROLLS", NULL);
   sscreen->shader_debug_flags = debug_get_flags_option("AMD_DEBUG", radeonsi_shader_debug_options, 0);

   if (sscreen->debug_flags & DBG(NO_DISPLAY_DCC)) {
      sscreen->info.use_display_dcc_unaligned = false;
      sscreen->info.use_display_dcc_with_retile_blit = false;
   }

   /* Using the environment variable doesn't enable PAIRS packets for simplicity. */
   if ((sscreen->debug_flags & DBG(SHADOW_REGS)) &&
       !(sscreen->info.userq_ip_mask & (1 << AMD_IP_GFX)))
      sscreen->info.has_kernelq_reg_shadowing = true;

#if AMD_LLVM_AVAILABLE
   sscreen->use_aco = support_aco && sscreen->info.has_image_opcodes &&
                      !(sscreen->shader_debug_flags & DBG(USE_LLVM));
#else
   sscreen->use_aco = true;
#endif

   if (sscreen->use_aco && !support_aco) {
      mesa_loge("ACO does not support this chip yet");
      return false;
   }

   si_setup_force_shader_use_aco(sscreen, support_aco);

   sscreen->b.set_max_shader_compiler_threads = si_set_max_shader_compiler_threads;
   sscreen->b.is_parallel_shader_compilation_finished = si_is_parallel_shader_compilation_finished;
   sscreen->b.finalize_nir = si_finalize_nir;

   sscreen->nir_options = CALLOC_STRUCT(nir_shader_compiler_options);

   si_init_screen_state_functions(sscreen);
   si_init_screen_query_functions(sscreen);
   si_init_screen_live_shader_cache(sscreen);

   si_init_screen_nir_options(sscreen);
   si_init_shader_caps(sscreen);
   si_init_compute_caps(sscreen);
   si_init_gfx_caps(sscreen);
   if (sscreen->b.caps.mesh_shader)
      si_init_mesh_caps(sscreen);

   sscreen->force_aniso = MIN2(16, debug_get_num_option("R600_TEX_ANISO", -1));
   if (sscreen->force_aniso == -1) {
      sscreen->force_aniso = MIN2(16, debug_get_num_option("AMD_TEX_ANISO", -1));
   }

   if (sscreen->force_aniso >= 0) {
      printf("radeonsi: Forcing anisotropy filter to %ix\n",
             /* round down to a power of two */
             1 << util_logbase2(sscreen->force_aniso));
   }

   (void)simple_mtx_init(&sscreen->async_compute_context_lock, mtx_plain);
   (void)simple_mtx_init(&sscreen->gpu_load_mutex, mtx_plain);
   (void)simple_mtx_init(&sscreen->gds_mutex, mtx_plain);
   (void)simple_mtx_init(&sscreen->tess_ring_lock, mtx_plain);

   si_init_gs_info(sscreen);
   if (!si_init_shader_cache(sscreen)) {
      FREE(sscreen->nir_options);
      return false;
   }

   if (sscreen->info.gfx_level < GFX10_3)
      sscreen->options.vrs2x2 = false;

   /* Determine the number of shader compiler threads. */
   const struct util_cpu_caps_t *caps = util_get_cpu_caps();
   hw_threads = caps->nr_cpus;

   if (hw_threads >= 12) {
      num_comp_hi_threads = hw_threads * 3 / 4;
      num_comp_lo_threads = hw_threads / 3;
   } else if (hw_threads >= 6) {
      num_comp_hi_threads = hw_threads - 2;
      num_comp_lo_threads = hw_threads / 2;
   } else if (hw_threads >= 2) {
      num_comp_hi_threads = hw_threads - 1;
      num_comp_lo_threads = hw_threads / 2;
   } else {
      num_comp_hi_threads = 1;
      num_comp_lo_threads = 1;
   }

#if !defined(NDEBUG)
   nir_process_debug_variable();

   /* Use a single compilation thread if NIR printing is enabled to avoid
    * multiple shaders being printed at the same time.
    */
   if (NIR_DEBUG(PRINT)) {
      num_comp_hi_threads = 1;
      num_comp_lo_threads = 1;
   }
#endif

   num_comp_hi_threads = MIN2(num_comp_hi_threads, ARRAY_SIZE(sscreen->compiler));
   num_comp_lo_threads = MIN2(num_comp_lo_threads, ARRAY_SIZE(sscreen->compiler_lowp));

   /* Take a reference on the glsl types for the compiler threads. */
   glsl_type_singleton_init_or_ref();

   /* Start with a single thread and a single slot.
    * Each time we'll hit the "all slots are in use" case, the number of threads and
    * slots will be increased.
    */
   int num_slots = num_comp_hi_threads == 1 ? 64 : 1;
   if (!util_queue_init(&sscreen->shader_compiler_queue, "sh", num_slots,
                        num_comp_hi_threads,
                        UTIL_QUEUE_INIT_RESIZE_IF_FULL |
                        UTIL_QUEUE_INIT_SET_FULL_THREAD_AFFINITY, NULL)) {
      si_destroy_shader_cache(sscreen);
      FREE(sscreen->nir_options);
      glsl_type_singleton_decref();
      return false;
   }

   if (!util_queue_init(&sscreen->shader_compiler_queue_opt_variants, "sh_opt", num_slots,
                        num_comp_lo_threads,
                        UTIL_QUEUE_INIT_RESIZE_IF_FULL |
                        UTIL_QUEUE_INIT_SET_FULL_THREAD_AFFINITY, NULL)) {
      si_destroy_shader_cache(sscreen);
      FREE(sscreen->nir_options);
      glsl_type_singleton_decref();
      return false;
   }

   if (!debug_get_bool_option("RADEON_DISABLE_PERFCOUNTERS", false))
      si_init_perfcounters(sscreen);

   if (sscreen->debug_flags & DBG(NO_OUT_OF_ORDER))
      sscreen->info.has_out_of_order_rast = false;

   /* Only set this for the cases that are known to work, which are:
    * - GFX9 if bpp >= 4 (in bytes)
    */
   if (sscreen->info.gfx_level >= GFX10) {
      memset(sscreen->allow_dcc_msaa_clear_to_reg_for_bpp, true,
             sizeof(sscreen->allow_dcc_msaa_clear_to_reg_for_bpp));
   } else if (sscreen->info.gfx_level == GFX9) {
      for (unsigned bpp_log2 = util_logbase2(1); bpp_log2 <= util_logbase2(16); bpp_log2++)
         sscreen->allow_dcc_msaa_clear_to_reg_for_bpp[bpp_log2] = true;
   }

   /* DCC stores have 50% performance of uncompressed stores and sometimes
    * even less than that. It's risky to enable on dGPUs.
    */
   sscreen->always_allow_dcc_stores = !(sscreen->debug_flags & DBG(NO_DCC_STORE)) &&
                                      (sscreen->debug_flags & DBG(DCC_STORE) ||
                                       sscreen->info.gfx_level >= GFX11 || /* always enabled on gfx11 */
                                       (sscreen->info.gfx_level >= GFX10_3 &&
                                        !sscreen->info.has_dedicated_vram));

   sscreen->dpbb_allowed = !(sscreen->debug_flags & DBG(NO_DPBB)) &&
                           (sscreen->info.gfx_level >= GFX10 ||
                            /* Only enable primitive binning on gfx9 APUs by default. */
                            (sscreen->info.gfx_level == GFX9 && !sscreen->info.has_dedicated_vram) ||
                            sscreen->debug_flags & DBG(DPBB));

   if (sscreen->dpbb_allowed) {
      if ((sscreen->info.has_dedicated_vram && sscreen->info.max_render_backends > 4) ||
	  sscreen->info.gfx_level >= GFX10) {
	 /* Only bin draws that have no CONTEXT and SH register changes between
	  * them because higher settings cause hangs. We've only been able to
	  * reproduce hangs on smaller chips (e.g. Navi24, Phoenix), though all
	  * chips might have them. What we see may be due to a driver bug.
	  */
         sscreen->pbb_context_states_per_bin = 1;
         sscreen->pbb_persistent_states_per_bin = 1;
      } else {
         /* This is a workaround for:
          *    https://bugs.freedesktop.org/show_bug.cgi?id=110214
          * (an alternative is to insert manual BATCH_BREAK event when
          *  a context_roll is detected). */
         sscreen->pbb_context_states_per_bin = sscreen->info.has_gfx9_scissor_bug ? 1 : 3;
         sscreen->pbb_persistent_states_per_bin = 8;
      }

      if (!sscreen->info.has_gfx9_scissor_bug)
         sscreen->pbb_context_states_per_bin =
            debug_get_num_option("AMD_DEBUG_DPBB_CS", sscreen->pbb_context_states_per_bin);
      sscreen->pbb_persistent_states_per_bin =
         debug_get_num_option("AMD_DEBUG_DPBB_PS", sscreen->pbb_persistent_states_per_bin);

      assert(sscreen->pbb_context_states_per_bin >= 1 &&
             sscreen->pbb_context_states_per_bin <= 6);
      assert(sscreen->pbb_persistent_states_per_bin >= 1 &&
             sscreen->pbb_persistent_states_per_bin <= 32);
   }

   (void)simple_mtx_init(&sscreen->shader_parts_mutex, mtx_plain);
   sscreen->use_monolithic_shaders =
      (sscreen->shader_debug_flags & DBG(MONOLITHIC_SHADERS)) != 0;

   if (debug_get_bool_option("RADEON_DUMP_SHADERS", false))
      sscreen->shader_debug_flags |= DBG_ALL_SHADERS;

   /* Syntax:
    *     EQAA=s,z,c
    * Example:
    *     EQAA=8,4,2

    * That means 8 coverage samples, 4 Z/S samples, and 2 color samples.
    * Constraints:
    *     s >= z >= c (ignoring this only wastes memory)
    *     s = [2..16]
    *     z = [2..8]
    *     c = [2..8]
    *
    * Only MSAA color and depth buffers are overridden.
    */
   if (sscreen->info.has_eqaa_surface_allocator) {
      const char *eqaa = debug_get_option("EQAA", NULL);
      unsigned s, z, f;

      if (eqaa && sscanf(eqaa, "%u,%u,%u", &s, &z, &f) == 3 && s && z && f) {
         sscreen->eqaa_force_coverage_samples = s;
         sscreen->eqaa_force_z_samples = z;
         sscreen->eqaa_force_color_samples = f;
      }
   }

   if (sscreen->info.gfx_level >= GFX11) {
      sscreen->attribute_pos_prim_ring =
         si_aligned_buffer_create(&sscreen->b,
                                  PIPE_RESOURCE_FLAG_UNMAPPABLE |
                                  SI_RESOURCE_FLAG_32BIT |
                                  SI_RESOURCE_FLAG_DRIVER_INTERNAL |
                                  SI_RESOURCE_FLAG_DISCARDABLE,
                                  PIPE_USAGE_DEFAULT,
                                  sscreen->info.total_attribute_pos_prim_ring_size,
                                  2 * 1024 * 1024);
   }

   ac_print_nonshadowed_regs(sscreen->info.gfx_level, sscreen->info.family);

   return true;
}

void si_fini_gfx_screen(struct si_screen *sscreen) {
   struct si_shader_part *parts[] = {sscreen->ps_prologs, sscreen->ps_epilogs};
   unsigned i;

   if (!sscreen->has_gfx_compute)
      return;

   if (sscreen->debug_flags & DBG(CACHE_STATS)) {
      printf("live shader cache:   hits = %u, misses = %u\n", sscreen->live_shader_cache.hits,
             sscreen->live_shader_cache.misses);
      printf("memory shader cache: hits = %u, misses = %u\n", sscreen->num_memory_shader_cache_hits,
             sscreen->num_memory_shader_cache_misses);
      printf("disk shader cache:   hits = %u, misses = %u\n", sscreen->num_disk_shader_cache_hits,
             sscreen->num_disk_shader_cache_misses);
   }

   si_resource_reference(&sscreen->attribute_pos_prim_ring, NULL);
   si_resource_reference(&sscreen->attribute_pos_prim_ring_tmz, NULL);
   pipe_resource_reference(&sscreen->tess_rings, NULL);
   pipe_resource_reference(&sscreen->tess_rings_tmz, NULL);

   util_queue_destroy(&sscreen->shader_compiler_queue);
   util_queue_destroy(&sscreen->shader_compiler_queue_opt_variants);

   simple_mtx_destroy(&sscreen->async_compute_context_lock);
   if (sscreen->async_compute_context)
      sscreen->async_compute_context->destroy(sscreen->async_compute_context);

   /* Release the reference on glsl types of the compiler threads. */
   glsl_type_singleton_decref();

   for (i = 0; i < ARRAY_SIZE(sscreen->compiler); i++) {
      if (sscreen->compiler[i])
         si_destroy_llvm_compiler(sscreen->compiler[i]);
   }

   for (i = 0; i < ARRAY_SIZE(sscreen->compiler_lowp); i++) {
      if (sscreen->compiler_lowp[i])
         si_destroy_llvm_compiler(sscreen->compiler_lowp[i]);
   }

   /* Free shader parts. */
   for (i = 0; i < ARRAY_SIZE(parts); i++) {
      while (parts[i]) {
         struct si_shader_part *part = parts[i];

         parts[i] = part->next;
         si_shader_binary_clean(&part->binary);
         FREE(part);
      }
   }
   simple_mtx_destroy(&sscreen->shader_parts_mutex);
   si_destroy_shader_cache(sscreen);

   si_destroy_perfcounters(sscreen);
   si_gpu_load_kill_thread(sscreen);

   simple_mtx_destroy(&sscreen->gpu_load_mutex);
   simple_mtx_destroy(&sscreen->gds_mutex);
   simple_mtx_destroy(&sscreen->tess_ring_lock);

   radeon_bo_reference(sscreen->ws, &sscreen->gds_oa, NULL);

   disk_cache_destroy(sscreen->disk_shader_cache);
   util_vertex_state_cache_deinit(&sscreen->vertex_state_cache);

   util_live_shader_cache_deinit(&sscreen->live_shader_cache);

   FREE(sscreen->use_aco_shader_blakes);
   FREE(sscreen->nir_options);
}
