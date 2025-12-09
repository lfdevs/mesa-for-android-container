/*
 * Copyright © 2010 - 2015 Intel Corporation
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
 */

#pragma once

#include <stdio.h>
#include "c11/threads.h"
#include "dev/intel_device_info.h"
#include "mda/debug_archiver.h"
#include "isl/isl.h"
#include "mda/debug_archiver.h"
#include "util/macros.h"
#include "util/mesa-sha1.h"
#include "util/enum_operators.h"
#include "util/ralloc.h"
#include "util/shader_stats.h"
#include "util/u_math.h"
#include "brw_isa_info.h"
#include "compiler/intel_shader_enums.h"
#include "nir_shader_compiler_options.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ra_regs;
struct nir_builder;
struct nir_def;
struct nir_shader;
struct shader_info;

typedef struct nir_builder nir_builder;
typedef struct nir_def nir_def;
typedef struct nir_shader nir_shader;

#define REG_CLASS_COUNT 20

struct brw_storage_format {
   uint32_t num_formats;
   uint32_t isl_formats[3];
};

struct brw_compiler {
   const struct intel_device_info *devinfo;

   /* This lock must be taken if the compiler is to be modified in any way,
    * including adding something to the ralloc child list.
    */
   mtx_t mutex;

   struct brw_isa_info isa;

   struct {
      struct ra_regs *regs;

      /**
       * Array of the ra classes for the unaligned contiguous register
       * block sizes used, indexed by register size.
       */
      struct ra_class *classes[REG_CLASS_COUNT];
   } reg_set;

   void (*shader_debug_log)(void *, unsigned *id, const char *str, ...) PRINTFLIKE(3, 4);
   void (*shader_perf_log)(void *, unsigned *id, const char *str, ...) PRINTFLIKE(3, 4);

   bool use_tcs_multi_patch;
   struct nir_shader_compiler_options nir_options[MESA_ALL_SHADER_STAGES];

   /**
    * Apply workarounds for SIN and COS output range problems.
    * This can negatively impact performance.
    */
   bool precise_trig;

   /**
    * Whether indirect UBO loads should use the sampler or go through the
    * data/constant cache.  For the sampler, UBO surface states have to be set
    * up with VK_FORMAT_R32G32B32A32_FLOAT whereas if it's going through the
    * constant or data cache, UBOs must use VK_FORMAT_RAW.
    */
   bool indirect_ubos_use_sampler;

   /**
    * Gfx12.5+ has a bit in the SEND instruction extending the bindless
    * surface offset range from 20 to 26 bits, effectively giving us 4Gb of
    * bindless surface descriptors instead of 64Mb previously.
    */
   bool extended_bindless_surface_offset;

   /**
    * Gfx11+ has a bit in the dword 3 of the sampler message header that
    * indicates whether the sampler handle is relative to the dynamic state
    * base address (0) or the bindless sampler base address (1). The driver
    * can select this.
    */
   bool use_bindless_sampler_offset;

   /**
    * Should DPAS instructions be lowered?
    *
    * This will be set for all platforms before Gfx12.5. It may also be set
    * platforms that support DPAS for testing purposes.
    */
   bool lower_dpas;

   /**
    * This can be set to use an "optimistic" SIMD heuristic that
    * assumes that the highest SIMD width and polygon count achievable
    * without spills will give the highest performance, so the
    * compiler doesn't need to try more than that.
    *
    * As of xe3 most programs compile without spills at 32-wide
    * dispatch so with this option enabled typically only a single
    * back-end compilation will be done instead of the default
    * behavior of one compilation per supported dispatch mode.  This
    * can speed up the back-end compilation of fragment shaders by a
    * 2+ factor, but could also increase compile-time especially on
    * pre-xe3 platforms in cases with high register pressure.
    *
    * Run-time performance of the shaders will be reduced since this
    * removes the ability to use a static analysis to estimate the
    * relative performance of the dispatch modes supported.
    */
   bool optimistic_simd_heuristic;

   /**
    * Calling the ra_allocate function after each register spill can take
    * several minutes. This option speeds up shader compilation by spilling
    * more registers after the ra_allocate failure. Required for
    * Cyberpunk 2077, which uses a watchdog thread to terminate the process
    * in case the render thread hasn't responded within 2 minutes.
    */
   int spilling_rate;

   /**
    * We perform a quick register pressure estimate at the NIR level before
    * attempting backend compilation at various SIMD widths.  If the estimated
    * register pressure for a given SIMD width is beyond this threshold, we
    * will skip FS and CS compilation at that width.
    *
    * This is vaguely the number of GRFs supported by the hardware, with a
    * bit of a fudge factor because we are only estimating, and sometimes
    * backend IR scheduling can reduce register pressure.
    */
   unsigned register_pressure_threshold;

   /**
    * A list of storage formats to lower from the matching return HW format.
    *
    * This list is used to build the lowering of read without format in
    * brw_nir_lower_storage_image.c
    */
   uint32_t num_lowered_storage_formats;
   uint32_t *lowered_storage_formats;
};

#define brw_shader_debug_log(compiler, data, fmt, ... ) do {    \
   static unsigned id = 0;                                      \
   compiler->shader_debug_log(data, &id, fmt, ##__VA_ARGS__);   \
} while (0)

#define brw_shader_perf_log(compiler, data, fmt, ... ) do {     \
   static unsigned id = 0;                                      \
   compiler->shader_perf_log(data, &id, fmt, ##__VA_ARGS__);    \
} while (0)

/**
 * We use a constant subgroup size of 32.  It really only needs to be a
 * maximum and, since we do SIMD32 for compute shaders in some cases, it
 * needs to be at least 32.  SIMD8 and SIMD16 shaders will still claim a
 * subgroup size of 32 but will act as if 16 or 24 of those channels are
 * disabled.
 */
#define BRW_SUBGROUP_SIZE 32

static inline bool
brw_shader_stage_is_bindless(mesa_shader_stage stage)
{
   return stage >= MESA_SHADER_RAYGEN &&
          stage <= MESA_SHADER_CALLABLE;
}

static inline bool
brw_shader_stage_requires_bindless_resources(mesa_shader_stage stage)
{
   return brw_shader_stage_is_bindless(stage) || mesa_shader_stage_is_mesh(stage);
}

static inline bool
brw_shader_stage_has_inline_data(const struct intel_device_info *devinfo,
                                 mesa_shader_stage stage)
{
   return stage == MESA_SHADER_MESH || stage == MESA_SHADER_TASK ||
          (stage == MESA_SHADER_COMPUTE && devinfo->verx10 >= 125);
}

/**
 * Program key structures.
 *
 * When drawing, we look for the currently bound shaders in the program
 * cache.  This is essentially a hash table lookup, and these are the keys.
 *
 * Sometimes OpenGL features specified as state need to be simulated via
 * shader code, due to a mismatch between the API and the hardware.  This
 * is often referred to as "non-orthagonal state" or "NOS".  We store NOS
 * in the program key so it's considered when searching for a program.  If
 * we haven't seen a particular combination before, we have to recompile a
 * new specialized version.
 *
 * Shader compilation should not look up state in gl_context directly, but
 * instead use the copy in the program key.  This guarantees recompiles will
 * happen correctly.
 *
 *  @{
 */

#define BRW_MAX_SAMPLERS 32

/* Provide explicit padding for each member, to ensure that the compiler
 * initializes every bit in the shader cache keys.  The keys will be compared
 * with memcmp.
 */
PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)

enum brw_robustness_flags {
   BRW_ROBUSTNESS_UBO  = BITFIELD_BIT(0),
   BRW_ROBUSTNESS_SSBO = BITFIELD_BIT(1),
};

struct brw_base_prog_key {
   /** Multiview mask
    *
    * Used to compute the number of position slots in the VUE
    */
   uint32_t view_mask;

   enum brw_robustness_flags robust_flags:2;

   bool uses_inline_push_addr:1;

   enum intel_vue_layout vue_layout:2;

   /**
    * Apply workarounds for SIN and COS input range problems.
    * This limits input range for SIN and COS to [-2p : 2p] to
    * avoid precision issues.
    */
   bool limit_trig_input_range:1;

   uint32_t padding:26;
};

/**
 * OpenGL attribute slots fall in [0, VERT_ATTRIB_MAX - 1] with the range
 * [VERT_ATTRIB_GENERIC0, VERT_ATTRIB_MAX - 1] reserved for up to 16 user
 * input vertex attributes. In Vulkan, we expose up to 29 user vertex input
 * attributes that are mapped to slots also starting at VERT_ATTRIB_GENERIC0.
 */
#define MAX_GL_VERT_ATTRIB     VERT_ATTRIB_MAX
#define MAX_VK_VERT_ATTRIB     (VERT_ATTRIB_GENERIC0 + 29)
#define MAX_HW_VERT_ATTRIB     (VERT_ATTRIB_GENERIC0 + 34)

/**
 * Use the last 2 slots :
 *   - slot 32: start vertex, vertex count, instance count, start instance
 *   - slot 33: base vertex, base instance, draw id
 */
#define BRW_SVGS_VE_INDEX (32)
#define BRW_DRAWID_VE_INDEX (33)

/** The program key for Vertex Shaders.
 *
 * Notes about slot compaction & component packing:
 *
 * VF slot compaction is our default compiler behavior. The compiler looks at
 * used inputs locations, for example [0, 2, 3], and will arrange things such
 * that the payload only includes 3 vec4 in that case. Location 1 is
 * completely dropped. The driver is expected to program
 * 3DSTATE_VERTEX_ELEMENTS to match this. So even if the location 1 is
 * described in the API input, the driver will not program it in
 * 3DSTATE_VERTEX_ELEMENTS because it sees the compiler is not using it.
 *
 * Component compaction is a HW feature that removes unused components (for
 * whatever slot [0, 31]) from the payload. Those values are stored in the URB
 * by the VF but they get scrapped when the payload is generated. For example
 * with input locations [ 0 vec2, 1 vec1, 2 vec4 ], the register payload for
 * VF inputs will be made up of 7 GRFs (2 + 1 + 4). Without component
 * compaction, the payload would be 12 GRFs (3 * 4).
 *
 * The HW component compaction feature only works on first 32 slots, so
 * anything after that will deliver the full vec4.
 */
struct brw_vs_prog_key {
   struct brw_base_prog_key base;

   /** Enable component packing
    *
    * Using this option requires that the driver programs
    * 3DSTATE_VF_COMPONENT_PACKING with the values provided in
    * brw_vs_prog_data::vf_component_packing
    */
   bool vf_component_packing : 1;

   /** Prevent compaction of slots of VF inputs
    *
    * So that 3DSTATE_VERTEX_ELEMENTS programming remains independent of
    * shader inputs (essentially an unused location should have an associated
    * VERTEX_ELEMENT_STATE).
    */
   bool no_vf_slot_compaction : 1;

   uint32_t padding:30;
};

/** The program key for Tessellation Control Shaders. */
struct brw_tcs_prog_key
{
   struct brw_base_prog_key base;

   /** A bitfield of per-vertex outputs written. */
   uint64_t outputs_written;

   enum tess_primitive_mode _tes_primitive_mode;

   /** Number of input vertices, 0 means dynamic */
   unsigned input_vertices;

   /** A bitfield of per-patch outputs written. */
   uint32_t patch_outputs_written;

   /** Tesselation VUE layout */
   bool separate_tess_vue_layout:1;

   uint32_t padding:31;
};

#define BRW_MAX_TCS_INPUT_VERTICES (32)

static inline uint32_t
brw_tcs_prog_key_input_vertices(const struct brw_tcs_prog_key *key)
{
   return key->input_vertices != 0 ?
          key->input_vertices : BRW_MAX_TCS_INPUT_VERTICES;
}

/** The program key for Tessellation Evaluation Shaders. */
struct brw_tes_prog_key
{
   struct brw_base_prog_key base;

   /** A bitfield of per-vertex inputs read. */
   uint64_t inputs_read;

   /** A bitfield of per-patch inputs read. */
   uint32_t patch_inputs_read;

   /** Tesselation VUE layout */
   bool separate_tess_vue_layout:1;

   uint32_t padding:31;
};

/** The program key for Geometry Shaders. */
struct brw_gs_prog_key
{
   struct brw_base_prog_key base;
};

struct brw_task_prog_key
{
   struct brw_base_prog_key base;
};

struct brw_mesh_prog_key
{
   struct brw_base_prog_key base;
};

/** The program key for Fragment/Pixel Shaders. */
struct brw_wm_prog_key {
   struct brw_base_prog_key base;

   float min_sample_shading;

   uint8_t color_outputs_valid;

   /* Some collection of BRW_WM_IZ_* */
   unsigned nr_color_regions:5;
   bool alpha_test_replicate_alpha:1;
   enum intel_sometimes alpha_to_coverage:2;

   bool force_dual_color_blend:1;

   /** Whether or inputs are interpolated at sample rate by default
    *
    * This corresponds to the sample shading API bit in Vulkan or OpenGL which
    * controls how inputs with no interpolation qualifier are interpolated.
    * This is distinct from the way that using gl_SampleID or similar requires
    * us to run per-sample.  Even when running per-sample due to gl_SampleID,
    * we may still interpolate unqualified inputs at the pixel center.
    */
   enum intel_sometimes persample_interp:2;

   /* Whether or not we are running on a multisampled framebuffer */
   enum intel_sometimes multisample_fbo:2;

   /* Whether the shader is dispatch with a preceeding mesh shader */
   enum intel_sometimes mesh_input:2;

   /* Is provoking vertex last? */
   enum intel_sometimes provoking_vertex_last:2;

   bool ignore_sample_mask_out:1;
   bool coarse_pixel:1;
   bool null_push_constant_tbimr_workaround:1;
   bool api_sample_shading:1;
   unsigned pad:3;
};

static inline bool
brw_wm_prog_key_is_dynamic(const struct brw_wm_prog_key *key)
{
   return
      key->mesh_input == INTEL_SOMETIMES ||
      key->provoking_vertex_last == INTEL_SOMETIMES ||
      key->alpha_to_coverage == INTEL_SOMETIMES ||
      key->persample_interp == INTEL_SOMETIMES ||
      key->multisample_fbo == INTEL_SOMETIMES ||
      key->base.vue_layout == INTEL_VUE_LAYOUT_SEPARATE_MESH;
}

struct brw_cs_prog_key {
   struct brw_base_prog_key base;

   /**
    * Lowers unaligned dispatches into aligned one by dispatching one more
    * extra workgroup and masking off excessive invocations in the shader.
    */
   bool lower_unaligned_dispatch:1;

   uint32_t padding:31;
};

struct brw_bs_prog_key {
   struct brw_base_prog_key base;

   /* Represents enum enum brw_rt_ray_flags values given at pipeline creation
    * to be combined with ray_flags handed to the traceRayEXT() calls by the
    * shader.
    */
   uint32_t pipeline_ray_flags;
};

/* brw_any_prog_key is any of the keys that map to an API stage */
union brw_any_prog_key {
   struct brw_base_prog_key base;
   struct brw_vs_prog_key vs;
   struct brw_tcs_prog_key tcs;
   struct brw_tes_prog_key tes;
   struct brw_gs_prog_key gs;
   struct brw_wm_prog_key wm;
   struct brw_cs_prog_key cs;
   struct brw_bs_prog_key bs;
   struct brw_task_prog_key task;
   struct brw_mesh_prog_key mesh;
};

PRAGMA_DIAGNOSTIC_POP

/** Max number of render targets in a shader */
#define BRW_MAX_DRAW_BUFFERS 8

struct brw_ubo_range
{
   uint16_t block;

   /* In units of 32-byte registers */
   uint8_t start;
   uint8_t length;
};

/* We reserve the first 2^16 values for builtins */
#define BRW_PARAM_IS_BUILTIN(param) (((param) & 0xffff0000) == 0)

enum brw_param_builtin {
   BRW_PARAM_BUILTIN_ZERO,

   BRW_PARAM_BUILTIN_CLIP_PLANE_0_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_0_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_0_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_0_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_1_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_1_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_1_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_1_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_2_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_2_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_2_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_2_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_3_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_3_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_3_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_3_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_4_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_4_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_4_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_4_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_5_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_5_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_5_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_5_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_6_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_6_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_6_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_6_W,
   BRW_PARAM_BUILTIN_CLIP_PLANE_7_X,
   BRW_PARAM_BUILTIN_CLIP_PLANE_7_Y,
   BRW_PARAM_BUILTIN_CLIP_PLANE_7_Z,
   BRW_PARAM_BUILTIN_CLIP_PLANE_7_W,

   BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_X,
   BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_Y,
   BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_Z,
   BRW_PARAM_BUILTIN_TESS_LEVEL_OUTER_W,
   BRW_PARAM_BUILTIN_TESS_LEVEL_INNER_X,
   BRW_PARAM_BUILTIN_TESS_LEVEL_INNER_Y,

   BRW_PARAM_BUILTIN_PATCH_VERTICES_IN,

   BRW_PARAM_BUILTIN_BASE_WORK_GROUP_ID_X,
   BRW_PARAM_BUILTIN_BASE_WORK_GROUP_ID_Y,
   BRW_PARAM_BUILTIN_BASE_WORK_GROUP_ID_Z,
   BRW_PARAM_BUILTIN_SUBGROUP_ID,
   BRW_PARAM_BUILTIN_WORK_GROUP_SIZE_X,
   BRW_PARAM_BUILTIN_WORK_GROUP_SIZE_Y,
   BRW_PARAM_BUILTIN_WORK_GROUP_SIZE_Z,
   BRW_PARAM_BUILTIN_WORK_DIM,
};

#define BRW_PARAM_BUILTIN_CLIP_PLANE(idx, comp) \
   (BRW_PARAM_BUILTIN_CLIP_PLANE_0_X + ((idx) << 2) + (comp))

#define BRW_PARAM_BUILTIN_IS_CLIP_PLANE(param)  \
   ((param) >= BRW_PARAM_BUILTIN_CLIP_PLANE_0_X && \
    (param) <= BRW_PARAM_BUILTIN_CLIP_PLANE_7_W)

#define BRW_PARAM_BUILTIN_CLIP_PLANE_IDX(param) \
   (((param) - BRW_PARAM_BUILTIN_CLIP_PLANE_0_X) >> 2)

#define BRW_PARAM_BUILTIN_CLIP_PLANE_COMP(param) \
   (((param) - BRW_PARAM_BUILTIN_CLIP_PLANE_0_X) & 0x3)

struct brw_stage_prog_data {
   struct brw_ubo_range ubo_ranges[4];

   unsigned nr_params;       /**< number of float params/constants */

   mesa_shader_stage stage;

   /* If robust_ubo_ranges not 0, push_reg_mask_param specifies the param
    * index (in 32-bit units) where the 4 UBO range limits will be pushed
    * as 8-bit integers. The shader will zero byte i of UBO range j if:
    *
    *    (robust_ubo_ranges & (1 << j)) &&
    *    (i < push_reg_mask_param[j] * 16)
    *
    * brw_compiler::compact_params must be false if robust_ubo_ranges used
    */
   uint8_t robust_ubo_ranges;
   unsigned push_reg_mask_param;

   unsigned curb_read_length;
   unsigned total_scratch;
   unsigned total_shared;

   unsigned program_size;

   unsigned const_data_size;
   unsigned const_data_offset;

   unsigned num_relocs;
   const struct intel_shader_reloc *relocs;

   /** Does this program pull from any UBO or other constant buffers? */
   bool has_ubo_pull;

   /** How many ray queries objects in this shader. */
   unsigned ray_queries;

   /**
    * Register where the thread expects to find input data from the URB
    * (typically uniforms, followed by vertex or fragment attributes).
    */
   unsigned dispatch_grf_start_reg;

   /** Number of GRF registers used. */
   unsigned grf_used;

   bool use_alt_mode; /**< Use ALT floating point mode?  Otherwise, IEEE. */

   uint32_t source_hash;

   /* 32-bit identifiers for all push/pull parameters.  These can be anything
    * the driver wishes them to be; the core of the back-end compiler simply
    * re-arranges them.  The one restriction is that the bottom 2^16 values
    * are reserved for builtins defined in the brw_param_builtin enum defined
    * above.
    */
   uint32_t *param;

   /* Whether shader uses atomic operations. */
   bool uses_atomic_load_store;
};

/**
 * Convert a number of GRF registers used (grf_used in prog_data) into
 * a number of GRF register blocks supported by the hardware on PTL+.
 */
unsigned ptl_register_blocks(unsigned grf_used);

enum brw_pixel_shader_computed_depth_mode {
   BRW_PSCDEPTH_OFF   = 0, /* PS does not compute depth */
   BRW_PSCDEPTH_ON    = 1, /* PS computes depth; no guarantee about value */
   BRW_PSCDEPTH_ON_GE = 2, /* PS guarantees output depth >= source depth */
   BRW_PSCDEPTH_ON_LE = 3, /* PS guarantees output depth <= source depth */
};

/* Data about a particular attempt to compile a program.  Note that
 * there can be many of these, each in a different GL state
 * corresponding to a different brw_wm_prog_key struct, with different
 * compiled programs.
 */
struct brw_wm_prog_data {
   struct brw_stage_prog_data base;

   /**
    * Number of slots (16B) chunks dedicated to per primitive payload.
    */
   unsigned num_per_primitive_inputs;

   /**
    * Number of slots (16B) chunks dedicated to per vertex payload.
    */
   unsigned num_varying_inputs;

   uint8_t dispatch_grf_start_reg_16;
   uint8_t dispatch_grf_start_reg_32;
   uint32_t prog_offset_16;
   uint32_t prog_offset_32;

   uint8_t computed_depth_mode;

   /**
    * Number of polygons handled in parallel by the multi-polygon PS
    * kernel.
    */
   uint8_t max_polygons;

   /**
    * Dispatch width of the multi-polygon PS kernel, or 0 if no
    * multi-polygon kernel was built.
    */
   uint8_t dispatch_multi;

   bool computed_stencil;
   bool early_fragment_tests;
   bool post_depth_coverage;
   bool inner_coverage;
   bool dispatch_8;
   bool dispatch_16;
   bool dispatch_32;
   bool dual_src_blend;
   bool uses_pos_offset;
   bool uses_omask;
   bool uses_kill;
   bool uses_src_depth;
   bool uses_src_w;
   bool uses_depth_w_coefficients;
   bool uses_pc_bary_coefficients;
   bool uses_npc_bary_coefficients;
   bool uses_sample_offsets;
   bool uses_sample_mask;
   bool uses_vmask;
   bool has_side_effects;
   bool pulls_bary;

   /**
    * Whether nonperspective interpolation modes are used by the
    * barycentric_interp_modes or fragment shader through interpolator messages.
    */
   bool uses_nonperspective_interp_modes;

   /** Fragment shader barycentrics
    *
    * Request that the HW delta computation of attributes be turned off. This is needed
    * when the FS has per-vertex inputs or reads BaryCoordKHR/BaryCoordNoPerspKHR.
    */
   bool vertex_attributes_bypass;

   /** True if the shader wants sample shading
    *
    * This corresponds to whether or not a gl_SampleId, gl_SamplePosition, or
    * a sample-qualified input are used in the shader.  It is independent of
    * GL_MIN_SAMPLE_SHADING_VALUE in GL or minSampleShading in Vulkan.
    */
   bool sample_shading;

   /** True if the API wants sample shading
    *
    * Not used by the compiler, but useful for restore from the cache. The
    * driver is expected to write the value it wants.
    */
   bool api_sample_shading;

   /** Min sample shading value
    *
    * Not used by the compiler, but useful for restore from the cache. The
    * driver is expected to write the value it wants.
    */
   float min_sample_shading;

   /** Should this shader be dispatched per-sample */
   enum intel_sometimes persample_dispatch;

   /**
    * Shader is ran at the coarse pixel shading dispatch rate (3DSTATE_CPS).
    */
   enum intel_sometimes coarse_pixel_dispatch;

   /**
    * Shader writes the SampleMask and this is AND-ed with the API's
    * SampleMask to generate a new coverage mask.
    */
   enum intel_sometimes alpha_to_coverage;

   /**
    * Whether the shader is dispatch with a preceeding mesh shader.
    */
   enum intel_sometimes mesh_input;

   /*
    * Provoking vertex may be dynamically set to last and we need to know
    * its value for fragment shader barycentrics and flat inputs.
    */
   enum intel_sometimes provoking_vertex_last;

   /**
    * Push constant location of intel_msaa_flags (dynamic configuration of the
    * pixel shader).
    */
   unsigned msaa_flags_param;

   /**
    * Push constant location of the remapping offset in the instruction heap
    * for Wa_18019110168.
    */
   unsigned per_primitive_remap_param;

   /**
    * Mask of which interpolation modes are required by the fragment shader.
    * Those interpolations are delivered as part of the thread payload. Used
    * in hardware setup on gfx6+.
    */
   uint32_t barycentric_interp_modes;

   /**
    * Mask of which FS inputs are marked flat by the shader source.  This is
    * needed for setting up 3DSTATE_SF/SBE.
    */
   uint32_t flat_inputs;

   /**
    * The FS inputs
    */
   uint64_t inputs;

   /**
    * The FS per-primitive inputs (some bits can be in both inputs &
    * per_primitive_inputs if the shader is compiled without being linked to
    * the previous stage)
    */
   uint64_t per_primitive_inputs;

   /**
    * Map from gl_varying_slot to the position within the FS setup data
    * payload where the varying's attribute vertex deltas should be delivered.
    * For varying slots that are not used by the FS, the value is -1.
    */
   int urb_setup[VARYING_SLOT_MAX];
   int urb_setup_channel[VARYING_SLOT_MAX];

   /**
    * Cache structure into the urb_setup array above that contains the
    * attribute numbers of active varyings out of urb_setup.
    * The actual count is stored in urb_setup_attribs_count.
    */
   uint8_t urb_setup_attribs[VARYING_SLOT_MAX];
   uint8_t urb_setup_attribs_count;
};

static inline bool
brw_wm_prog_data_is_dynamic(const struct brw_wm_prog_data *prog_data)
{
   return prog_data->mesh_input == INTEL_SOMETIMES ||
      (prog_data->vertex_attributes_bypass &&
       prog_data->provoking_vertex_last == INTEL_SOMETIMES) ||
      prog_data->alpha_to_coverage == INTEL_SOMETIMES ||
      prog_data->coarse_pixel_dispatch == INTEL_SOMETIMES ||
      prog_data->persample_dispatch == INTEL_SOMETIMES;
}

#ifdef GFX_VERx10

#if GFX_VERx10 >= 200

/** Returns the SIMD width corresponding to a given KSP index
 *
 * The "Variable Pixel Dispatch" table in the PRM (which can be found, for
 * example in Vol. 7 of the SKL PRM) has a mapping from dispatch widths to
 * kernel start pointer (KSP) indices that is based on what dispatch widths
 * are enabled.  This function provides, effectively, the reverse mapping.
 *
 * If the given KSP is enabled, a SIMD width of 8, 16, or 32 is
 * returned.  Note that for a multipolygon dispatch kernel 8 is always
 * returned, since multipolygon kernels use the "_8" fields from
 * brw_wm_prog_data regardless of their SIMD width.  If the KSP is
 * invalid, 0 is returned.
 */
static inline unsigned
brw_fs_simd_width_for_ksp(unsigned ksp_idx, bool enabled, unsigned width_sel)
{
   assert(ksp_idx < 2);
   return !enabled ? 0 :
          width_sel ? 32 :
          16;
}

#define brw_wm_state_simd_width_for_ksp(wm_state, ksp_idx)              \
        (ksp_idx == 0 && (wm_state).Kernel0MaximumPolysperThread ? 8 :  \
         ksp_idx == 0 ? brw_fs_simd_width_for_ksp(ksp_idx, (wm_state).Kernel0Enable, \
                                                  (wm_state).Kernel0SIMDWidth): \
         brw_fs_simd_width_for_ksp(ksp_idx, (wm_state).Kernel1Enable,   \
                                   (wm_state).Kernel1SIMDWidth))

#else

/** Returns the SIMD width corresponding to a given KSP index
 *
 * The "Variable Pixel Dispatch" table in the PRM (which can be found, for
 * example in Vol. 7 of the SKL PRM) has a mapping from dispatch widths to
 * kernel start pointer (KSP) indices that is based on what dispatch widths
 * are enabled.  This function provides, effectively, the reverse mapping.
 *
 * If the given KSP is valid with respect to the SIMD8/16/32 enables, a SIMD
 * width of 8, 16, or 32 is returned.  If the KSP is invalid, 0 is returned.
 */
static inline unsigned
brw_fs_simd_width_for_ksp(unsigned ksp_idx, bool simd8_enabled,
                          bool simd16_enabled, bool simd32_enabled)
{
   /* This function strictly ignores contiguous dispatch */
   switch (ksp_idx) {
   case 0:
      return simd8_enabled ? 8 :
             (simd16_enabled && !simd32_enabled) ? 16 :
             (simd32_enabled && !simd16_enabled) ? 32 : 0;
   case 1:
      return (simd32_enabled && (simd16_enabled || simd8_enabled)) ? 32 : 0;
   case 2:
      return (simd16_enabled && (simd32_enabled || simd8_enabled)) ? 16 : 0;
   default:
      UNREACHABLE("Invalid KSP index");
   }
}

#define brw_wm_state_simd_width_for_ksp(wm_state, ksp_idx)              \
   brw_fs_simd_width_for_ksp((ksp_idx), (wm_state)._8PixelDispatchEnable, \
                             (wm_state)._16PixelDispatchEnable, \
                             (wm_state)._32PixelDispatchEnable)

#endif

#endif

#define brw_wm_state_has_ksp(wm_state, ksp_idx) \
   (brw_wm_state_simd_width_for_ksp((wm_state), (ksp_idx)) != 0)

static inline uint32_t
_brw_wm_prog_data_prog_offset(const struct brw_wm_prog_data *prog_data,
                              unsigned simd_width)
{
   switch (simd_width) {
   case 8: return 0;
   case 16: return prog_data->prog_offset_16;
   case 32: return prog_data->prog_offset_32;
   default: return 0;
   }
}

#define brw_wm_prog_data_prog_offset(prog_data, wm_state, ksp_idx) \
   _brw_wm_prog_data_prog_offset(prog_data, \
      brw_wm_state_simd_width_for_ksp(wm_state, ksp_idx))

static inline uint8_t
_brw_wm_prog_data_dispatch_grf_start_reg(const struct brw_wm_prog_data *prog_data,
                                         unsigned simd_width)
{
   switch (simd_width) {
   case 8: return prog_data->base.dispatch_grf_start_reg;
   case 16: return prog_data->dispatch_grf_start_reg_16;
   case 32: return prog_data->dispatch_grf_start_reg_32;
   default: return 0;
   }
}

#define brw_wm_prog_data_dispatch_grf_start_reg(prog_data, wm_state, ksp_idx) \
   _brw_wm_prog_data_dispatch_grf_start_reg(prog_data, \
      brw_wm_state_simd_width_for_ksp(wm_state, ksp_idx))

static inline bool
brw_wm_prog_data_is_persample(const struct brw_wm_prog_data *prog_data,
                              enum intel_msaa_flags pushed_msaa_flags)
{
   return intel_fs_is_persample(prog_data->persample_dispatch,
                                prog_data->sample_shading,
                                pushed_msaa_flags);
}

static inline uint32_t
wm_prog_data_barycentric_modes(const struct brw_wm_prog_data *prog_data,
                               enum intel_msaa_flags pushed_msaa_flags)
{
   return intel_fs_barycentric_modes(prog_data->persample_dispatch,
                                     prog_data->barycentric_interp_modes,
                                     pushed_msaa_flags);
}

static inline bool
brw_wm_prog_data_is_coarse(const struct brw_wm_prog_data *prog_data,
                           enum intel_msaa_flags pushed_msaa_flags)
{
   return intel_fs_is_coarse(prog_data->coarse_pixel_dispatch,
                             pushed_msaa_flags);
}

struct brw_push_const_block {
   unsigned dwords;     /* Dword count, not reg aligned */
   unsigned regs;
   unsigned size;       /* Bytes, register aligned */
};

struct brw_cs_prog_data {
   struct brw_stage_prog_data base;

   unsigned local_size[3];

   /* Program offsets for the 8/16/32 SIMD variants.  Multiple variants are
    * kept when using variable group size, and the right one can only be
    * decided at dispatch time.
    */
   unsigned prog_offset[3];

   /* Bitmask indicating which program offsets are valid. */
   unsigned prog_mask;

   /* Bitmask indicating which programs have spilled. */
   unsigned prog_spilled;

   bool uses_barrier;
   bool uses_inline_data;
   /** Whether inline push data is used to provide a 64bit pointer to push
    * constants
    */
   bool uses_inline_push_addr;
   bool uses_btd_stack_ids;
   bool uses_systolic;
   uint8_t generate_local_id;
   enum intel_compute_walk_order walk_order;

   /* True if shader has any sample operation */
   bool uses_sampler;

   struct {
      struct brw_push_const_block cross_thread;
      struct brw_push_const_block per_thread;
   } push;
};

static inline uint32_t
brw_cs_prog_data_prog_offset(const struct brw_cs_prog_data *prog_data,
                             unsigned dispatch_width)
{
   assert(dispatch_width == 8 ||
          dispatch_width == 16 ||
          dispatch_width == 32);
   const unsigned index = dispatch_width / 16;
   assert(prog_data->prog_mask & (1 << index));
   return prog_data->prog_offset[index];
}

struct brw_bs_prog_data {
   struct brw_stage_prog_data base;

   /** Whether inline push data is used to provide a 64bit pointer to push
    * constants
    */
   bool uses_inline_push_addr;

   /** SIMD size of the root shader */
   uint8_t simd_size;

   /** Maximum stack size of all shaders */
   uint32_t max_stack_size;

   /** Offset into the shader where the resume SBT is located */
   uint32_t resume_sbt_offset;

   /** Number of resume shaders */
   uint32_t num_resume_shaders;
};

/**
 * Enum representing the i965-specific vertex results that don't correspond
 * exactly to any element of gl_varying_slot.  The values of this enum are
 * assigned such that they don't conflict with gl_varying_slot.
 */
typedef enum
{
   BRW_VARYING_SLOT_PAD = VARYING_SLOT_MAX,
   BRW_VARYING_SLOT_COUNT
} brw_varying_slot;


#define BRW_VUE_HEADER_VARYING_MASK \
   (VARYING_BIT_VIEWPORT | \
    VARYING_BIT_LAYER | \
    VARYING_BIT_PRIMITIVE_SHADING_RATE | \
    VARYING_BIT_PSIZ)

/**
 * Bitmask indicating which fragment shader inputs represent varyings (and
 * hence have to be delivered to the fragment shader by the SF/SBE stage).
 */
#define BRW_FS_VARYING_INPUT_MASK \
   (BITFIELD64_RANGE(0, VARYING_SLOT_MAX) & \
    ~VARYING_BIT_POS & ~VARYING_BIT_FACE)

void brw_print_vue_map(FILE *fp, const struct intel_vue_map *vue_map,
                       mesa_shader_stage stage);

/**
 * Convert a VUE slot number into a byte offset within the VUE.
 */
static inline unsigned brw_vue_slot_to_offset(unsigned slot)
{
   return 16*slot;
}

/**
 * Convert a vertex output (brw_varying_slot) into a byte offset within the
 * VUE.
 */
static inline unsigned
brw_varying_to_offset(const struct intel_vue_map *vue_map, unsigned varying)
{
   return brw_vue_slot_to_offset(vue_map->varying_to_slot[varying]);
}

void
brw_compute_per_primitive_map(int *out_per_primitive_map,
                              uint32_t *out_per_primitive_stride,
                              uint32_t *out_first_offset,
                              uint32_t base_offset,
                              nir_shader *nir,
                              uint32_t variables_mode,
                              uint64_t slots_valid,
                              bool separate_shader);

void brw_compute_vue_map(const struct intel_device_info *devinfo,
                         struct intel_vue_map *vue_map,
                         uint64_t slots_valid,
                         enum intel_vue_layout layout,
                         uint32_t pos_slots);

void brw_compute_tess_vue_map(struct intel_vue_map *const vue_map,
                              uint64_t slots_valid,
                              uint32_t is_patch,
                              bool separate_shader);

struct brw_vue_prog_data {
   struct brw_stage_prog_data base;
   struct intel_vue_map vue_map;

   /** Should the hardware deliver input VUE handles for URB pull loads? */
   bool include_vue_handles;

   unsigned urb_read_length;
   unsigned total_grf;

   uint32_t clip_distance_mask;
   uint32_t cull_distance_mask;

   /* Used for calculating urb partitions.  In the VS, this is the size of the
    * URB entry used for both input and output to the thread.  In the GS, this
    * is the size of the URB entry used for output.
    */
   unsigned urb_entry_size;

   enum intel_shader_dispatch_mode dispatch_mode;
};

struct brw_vs_prog_data {
   struct brw_vue_prog_data base;

   uint64_t inputs_read;
   uint64_t double_inputs_read;

   bool uses_vertexid;
   bool uses_instanceid;
   bool uses_is_indexed_draw;
   bool uses_firstvertex;
   bool uses_baseinstance;
   bool uses_drawid;
   bool no_vf_slot_compaction;

   uint32_t vf_component_packing[4];
};

struct brw_tess_info {
   enum tess_primitive_mode primitive_mode:8;
   uint8_t                         spacing:2;
   bool                                ccw:1;
   bool                         point_mode:1;
   uint32_t                            pad:20;
};

struct brw_tcs_prog_data
{
   struct brw_vue_prog_data base;

   struct brw_tess_info tess_info;

   /** Number of input vertices, 0 means dynamic */
   unsigned input_vertices;

   /** Number of output vertices */
   unsigned output_vertices;

   /** Should the non-SINGLE_PATCH payload provide primitive ID? */
   bool include_primitive_id;

   /** Whether the tessellation domain is unknown at compile time
    *
    * Used with VK_EXT_shader_object
    */
   bool dynamic_domain;

   /** Number vertices in output patch */
   int instances;

   /** Track patch count threshold */
   int patch_count_threshold;

   /**
    * Push constant location of intel_tess_config (dynamic configuration of
    * the tessellation shaders).
    */
   unsigned tess_config_param;
};

struct brw_tes_prog_data
{
   struct brw_vue_prog_data base;

   struct brw_tess_info tess_info;

   bool include_primitive_id;

   /**
    * Push constant location of intel_tess_config (dynamic configuration of
    * the tessellation shaders).
    */
   unsigned tess_config_param;
};

struct brw_gs_prog_data
{
   struct brw_vue_prog_data base;

   unsigned vertices_in;

   /**
    * Size of an output vertex, measured in HWORDS (32 bytes).
    */
   unsigned output_vertex_size_hwords;

   unsigned output_topology;

   /**
    * Size of the control data (cut bits or StreamID bits), in hwords (32
    * bytes).  0 if there is no control data.
    */
   unsigned control_data_header_size_hwords;

   /**
    * Format of the control data (either GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_SID
    * if the control data is StreamID bits, or
    * GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT if the control data is cut bits).
    * Ignored if control_data_header_size is 0.
    */
   unsigned control_data_format;

   bool include_primitive_id;

   /**
    * The number of vertices emitted, if constant - otherwise -1.
    */
   int static_vertex_count;

   int invocations;
};

struct brw_tue_map {
   uint32_t size_dw;
};

struct brw_mue_map {
   /* Total size in bytes of the MUE (32B aligned) */
   uint32_t size;

   uint32_t max_primitives;
   uint32_t max_vertices;

   /* Stride in bytes between sets of primitive indices */
   uint32_t per_primitive_indices_stride;

   /* Per primitive offset from the start of the MUE (32B aligned) */
   uint32_t per_primitive_offset;

   /* Per primitive stride in bytes (32B aligned) */
   uint32_t per_primitive_stride;

   /* Whether the per primitive block includes a header */
   bool has_per_primitive_header;

   /* Per vertex offset in bytes from the start of the MUE (32B aligned) */
   uint32_t per_vertex_offset;

   /* Size of the per vertex header (32B aligned) */
   uint32_t per_vertex_header_size;

   /* Per vertex stride in bytes (32B aligned) */
   uint32_t per_vertex_stride;

   /* VUE map for the per vertex attributes */
   struct intel_vue_map vue_map;

   bool wa_18019110168_active;

   /* Offset in bytes of each per primitive relative to
    * per_primitive_offset (-1 if unused)
    */
   int per_primitive_offsets[VARYING_SLOT_MAX];
};

struct brw_task_prog_data {
   struct brw_cs_prog_data base;
   struct brw_tue_map map;
   bool uses_drawid;
};

enum brw_mesh_index_format {
   BRW_INDEX_FORMAT_U32,
   BRW_INDEX_FORMAT_U888X,
};

struct brw_mesh_prog_data {
   struct brw_cs_prog_data base;
   struct brw_mue_map map;

   uint32_t clip_distance_mask;
   uint32_t cull_distance_mask;
   uint16_t primitive_type;

   enum brw_mesh_index_format index_format;

   bool uses_drawid;
   bool autostrip_enable;

   /**
    * Offset in the program where the remapping table for Wa_18019110168 is
    * located.
    *
    * The remapping table has the same format as
    * intel_vue_map::varying_to_slot[]
    */
   uint32_t wa_18019110168_mapping_offset;
};

/* brw_any_prog_data is prog_data for any stage that maps to an API stage */
union brw_any_prog_data {
   struct brw_stage_prog_data base;
   struct brw_vue_prog_data vue;
   struct brw_vs_prog_data vs;
   struct brw_tcs_prog_data tcs;
   struct brw_tes_prog_data tes;
   struct brw_gs_prog_data gs;
   struct brw_wm_prog_data wm;
   struct brw_cs_prog_data cs;
   struct brw_bs_prog_data bs;
   struct brw_task_prog_data task;
   struct brw_mesh_prog_data mesh;
};

#define DEFINE_PROG_DATA_DOWNCAST(STAGE, CHECK)                            \
static inline struct brw_##STAGE##_prog_data *                             \
brw_##STAGE##_prog_data(struct brw_stage_prog_data *prog_data)             \
{                                                                          \
   if (prog_data)                                                          \
      assert(CHECK);                                                       \
   return (struct brw_##STAGE##_prog_data *) prog_data;                    \
}                                                                          \
static inline const struct brw_##STAGE##_prog_data *                       \
brw_##STAGE##_prog_data_const(const struct brw_stage_prog_data *prog_data) \
{                                                                          \
   if (prog_data)                                                          \
      assert(CHECK);                                                       \
   return (const struct brw_##STAGE##_prog_data *) prog_data;              \
}

DEFINE_PROG_DATA_DOWNCAST(vs,  prog_data->stage == MESA_SHADER_VERTEX)
DEFINE_PROG_DATA_DOWNCAST(tcs, prog_data->stage == MESA_SHADER_TESS_CTRL)
DEFINE_PROG_DATA_DOWNCAST(tes, prog_data->stage == MESA_SHADER_TESS_EVAL)
DEFINE_PROG_DATA_DOWNCAST(gs,  prog_data->stage == MESA_SHADER_GEOMETRY)
DEFINE_PROG_DATA_DOWNCAST(wm,  prog_data->stage == MESA_SHADER_FRAGMENT)
DEFINE_PROG_DATA_DOWNCAST(cs,  mesa_shader_stage_uses_workgroup(prog_data->stage))
DEFINE_PROG_DATA_DOWNCAST(bs,  brw_shader_stage_is_bindless(prog_data->stage))

DEFINE_PROG_DATA_DOWNCAST(vue, prog_data->stage == MESA_SHADER_VERTEX ||
                               prog_data->stage == MESA_SHADER_TESS_CTRL ||
                               prog_data->stage == MESA_SHADER_TESS_EVAL ||
                               prog_data->stage == MESA_SHADER_GEOMETRY)

DEFINE_PROG_DATA_DOWNCAST(task, prog_data->stage == MESA_SHADER_TASK)
DEFINE_PROG_DATA_DOWNCAST(mesh, prog_data->stage == MESA_SHADER_MESH)

#undef DEFINE_PROG_DATA_DOWNCAST

static inline struct brw_tess_info
brw_merge_tess_info(struct brw_tess_info tcs_info,
                    struct brw_tess_info tes_info)
{
   /* Just merge by OR'ing the raw bits */
   uint32_t x, y;

   assert(sizeof(x) == sizeof(tcs_info));

   memcpy(&x, &tcs_info, sizeof(x));
   memcpy(&y, &tes_info, sizeof(y));

   x |= y;

   struct brw_tess_info out;
   memcpy(&out, &x, sizeof(out));
   return out;
}

static inline enum intel_tess_partitioning
brw_tess_info_partitioning(struct brw_tess_info info)
{
   return (enum intel_tess_partitioning)(info.spacing - 1);
}

static inline enum intel_tess_domain
brw_tess_info_domain(struct brw_tess_info info)
{
   switch (info.primitive_mode) {
   case TESS_PRIMITIVE_QUADS:
      return INTEL_TESS_DOMAIN_QUAD;
      break;
   case TESS_PRIMITIVE_TRIANGLES:
      return INTEL_TESS_DOMAIN_TRI;
      break;
   case TESS_PRIMITIVE_ISOLINES:
      return INTEL_TESS_DOMAIN_ISOLINE;
      break;
   default:
      UNREACHABLE("invalid primitive mode");
   }
}

static inline enum intel_tess_output_topology
brw_tess_info_output_topology(struct brw_tess_info info)
{
   if (info.point_mode) {
      return INTEL_TESS_OUTPUT_TOPOLOGY_POINT;
   } else if (info.primitive_mode == TESS_PRIMITIVE_ISOLINES) {
      return INTEL_TESS_OUTPUT_TOPOLOGY_LINE;
   } else {
      /* Hardware winding order is backwards from OpenGL */
      return info.ccw ?
         INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CW :
         INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CCW;
   }
}

/** @} */

struct brw_compiler *
brw_compiler_create(void *mem_ctx, const struct intel_device_info *devinfo);

/**
 * Returns a compiler configuration for use with disk shader cache
 *
 * This value only needs to change for settings that can cause different
 * program generation between two runs on the same hardware.
 *
 * For example, it doesn't need to be different for gen 8 and gen 9 hardware,
 * but it does need to be different if INTEL_DEBUG=nocompact is or isn't used.
 */
uint64_t
brw_get_compiler_config_value(const struct brw_compiler *compiler);

/* Provides a string sha1 hash of all device information fields that could
 * affect shader compilation.
 */
void
brw_device_sha1(char *hex, const struct intel_device_info *devinfo);

/* For callers computing their own UUID or hash.  Hashes all device
 * information fields that could affect shader compilation into the provided
 * sha1_ctx.
 */
void
brw_device_sha1_update(struct mesa_sha1 *sha1_ctx,
                       const struct intel_device_info *devinfo);

unsigned
brw_prog_data_size(mesa_shader_stage stage);

unsigned
brw_prog_key_size(mesa_shader_stage stage);

struct brw_compile_params {
   void *mem_ctx;

   nir_shader *nir;

   struct genisa_stats *stats;

   void *log_data;

   char *error_str;

   uint64_t debug_flag;

   uint32_t source_hash;

   debug_archiver *archiver;
};

/**
 * Parameters for compiling a vertex shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_vs_params {
   struct brw_compile_params base;

   const struct brw_vs_prog_key *key;
   struct brw_vs_prog_data *prog_data;
};

/**
 * Compile a vertex shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_vs(const struct brw_compiler *compiler,
               struct brw_compile_vs_params *params);

/**
 * Parameters for compiling a tessellation control shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_tcs_params {
   struct brw_compile_params base;

   const struct brw_tcs_prog_key *key;
   struct brw_tcs_prog_data *prog_data;
};

/**
 * Compile a tessellation control shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_tcs(const struct brw_compiler *compiler,
                struct brw_compile_tcs_params *params);

/**
 * Parameters for compiling a tessellation evaluation shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_tes_params {
   struct brw_compile_params base;

   const struct brw_tes_prog_key *key;
   struct brw_tes_prog_data *prog_data;
   const struct intel_vue_map *input_vue_map;
};

/**
 * Compile a tessellation evaluation shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_tes(const struct brw_compiler *compiler,
                struct brw_compile_tes_params *params);

/**
 * Parameters for compiling a geometry shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_gs_params {
   struct brw_compile_params base;

   const struct brw_gs_prog_key *key;
   struct brw_gs_prog_data *prog_data;
};

/**
 * Compile a geometry shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_gs(const struct brw_compiler *compiler,
               struct brw_compile_gs_params *params);

struct brw_compile_task_params {
   struct brw_compile_params base;

   const struct brw_task_prog_key *key;
   struct brw_task_prog_data *prog_data;
};

const unsigned *
brw_compile_task(const struct brw_compiler *compiler,
                 struct brw_compile_task_params *params);

struct brw_compile_mesh_params {
   struct brw_compile_params base;

   const struct brw_mesh_prog_key *key;
   struct brw_mesh_prog_data *prog_data;
   const struct brw_tue_map *tue_map;

   /** Load provoking vertex
    *
    * The callback returns a 32bit integer representing the provoking vertex.
    */
   void *load_provoking_vertex_data;
   nir_def *(*load_provoking_vertex)(nir_builder *b, void *data);
};

const unsigned *
brw_compile_mesh(const struct brw_compiler *compiler,
                 struct brw_compile_mesh_params *params);

/**
 * Parameters for compiling a fragment shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_fs_params {
   struct brw_compile_params base;

   const struct brw_wm_prog_key *key;
   struct brw_wm_prog_data *prog_data;

   const struct intel_vue_map *vue_map;
   const struct brw_mue_map *mue_map;

   bool allow_spilling;
   bool use_rep_send;
   uint8_t max_polygons;
};

/**
 * Compile a fragment shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_fs(const struct brw_compiler *compiler,
               struct brw_compile_fs_params *params);

/**
 * Parameters for compiling a compute shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_cs_params {
   struct brw_compile_params base;

   const struct brw_cs_prog_key *key;
   struct brw_cs_prog_data *prog_data;
};

/**
 * Compile a compute shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_cs(const struct brw_compiler *compiler,
               struct brw_compile_cs_params *params);

/**
 * Parameters for compiling a Bindless shader.
 *
 * Some of these will be modified during the shader compilation.
 */
struct brw_compile_bs_params {
   struct brw_compile_params base;

   const struct brw_bs_prog_key *key;
   struct brw_bs_prog_data *prog_data;

   unsigned num_resume_shaders;
   struct nir_shader **resume_shaders;
};

/**
 * Compile a Bindless shader.
 *
 * Returns the final assembly and updates the parameters structure.
 */
const unsigned *
brw_compile_bs(const struct brw_compiler *compiler,
               struct brw_compile_bs_params *params);

void brw_debug_key_recompile(const struct brw_compiler *c, void *log,
                             mesa_shader_stage stage,
                             const struct brw_base_prog_key *old_key,
                             const struct brw_base_prog_key *key);

unsigned
brw_cs_push_const_total_size(const struct brw_cs_prog_data *cs_prog_data,
                             unsigned threads);

void
brw_write_shader_relocs(const struct brw_isa_info *isa,
                        void *program,
                        const struct brw_stage_prog_data *prog_data,
                        struct intel_shader_reloc_value *values,
                        unsigned num_values);

/**
 * Get the dispatch information for a shader to be used with GPGPU_WALKER and
 * similar instructions.
 *
 * If override_local_size is not NULL, it must to point to a 3-element that
 * will override the value from prog_data->local_size.  This is used by
 * ARB_compute_variable_group_size, where the size is set only at dispatch
 * time (so prog_data is outdated).
 */
struct intel_cs_dispatch_info
brw_cs_get_dispatch_info(const struct intel_device_info *devinfo,
                         const struct brw_cs_prog_data *prog_data,
                         const unsigned *override_local_size);

/**
 * Return true if the given shader stage is dispatched contiguously by the
 * relevant fixed function starting from channel 0 of the SIMD thread, which
 * implies that the dispatch mask of a thread can be assumed to have the form
 * '2^n - 1' for some n.
 */
static inline bool
brw_stage_has_packed_dispatch(ASSERTED const struct intel_device_info *devinfo,
                              mesa_shader_stage stage, unsigned max_polygons,
                              const struct brw_stage_prog_data *prog_data)
{
   /* The code below makes assumptions about the hardware's thread dispatch
    * behavior that could be proven wrong in future generations -- Make sure
    * to do a full test run with brw_test_dispatch_packing() hooked up to
    * the NIR front-end before changing this assertion. It can be temporarily
    * enabled by setting the macro below to true.
    */
   #define ENABLE_TEST_DISPATCH_PACKING false
   assert(devinfo->ver <= 30);

   switch (stage) {
   case MESA_SHADER_FRAGMENT: {
      /* The PSD discards subspans coming in with no lit samples, which in the
       * per-pixel shading case implies that each subspan will either be fully
       * lit (due to the VMask being used to allow derivative computations),
       * or not dispatched at all.  In per-sample dispatch mode individual
       * samples from the same subspan have a fixed relative location within
       * the SIMD thread, so dispatch of unlit samples cannot be avoided in
       * general and we should return false.
       */
      const struct brw_wm_prog_data *wm_prog_data =
         (const struct brw_wm_prog_data *)prog_data;
      return devinfo->verx10 < 125 &&
             !wm_prog_data->persample_dispatch &&
             wm_prog_data->uses_vmask &&
             max_polygons < 2;
   }
   case MESA_SHADER_COMPUTE:
      /* Compute shaders will be spawned with either a fully enabled dispatch
       * mask or with whatever bottom/right execution mask was given to the
       * GPGPU walker command to be used along the workgroup edges -- In both
       * cases the dispatch mask is required to be tightly packed for our
       * invocation index calculations to work.
       */
      return true;
   default:
      /* Most remaining fixed functions are limited to use a packed dispatch
       * mask due to the hardware representation of the dispatch mask as a
       * single counter representing the number of enabled channels.
       */
      return true;
   }
}

/**
 * Computes the first varying slot in the URB produced by the previous stage
 * that is used in the next stage. We do this by testing the varying slots in
 * the previous stage's vue map against the inputs read in the next stage.
 *
 * Note that each URB offset contains two varying slots and we can only skip a
 * full offset if both slots are unused, so the value we return here is always
 * rounded down to the closest multiple of two.
 */
int
brw_compute_first_fs_urb_slot_required(uint64_t inputs_read,
                                       const struct intel_vue_map *prev_stage_vue_map,
                                       bool mesh);

void
brw_compute_sbe_per_vertex_urb_read(const struct intel_vue_map *prev_stage_vue_map,
                                    bool mesh, bool per_primitive_remapping,
                                    const struct brw_wm_prog_data *wm_prog_data,
                                    uint32_t *out_first_slot,
                                    uint32_t *num_slots,
                                    uint32_t *out_num_varyings,
                                    uint32_t *out_primitive_id_offset,
                                    uint32_t *out_flat_inputs);

/**
 * Computes the URB offset at which SBE should read the per primitive date
 * written by the mesh shader.
 */
void
brw_compute_sbe_per_primitive_urb_read(uint64_t inputs_read,
                                       uint32_t num_varyings,
                                       const struct brw_mue_map *mue_map,
                                       uint32_t *out_read_offset,
                                       uint32_t *out_read_length);

/* From InlineData in 3DSTATE_TASK_SHADER_DATA and 3DSTATE_MESH_SHADER_DATA. */
#define BRW_TASK_MESH_INLINE_DATA_SIZE_DW 8

/* InlineData[0-1] is used for Vulkan descriptor. */
#define BRW_TASK_MESH_PUSH_CONSTANTS_START_DW 2

#define BRW_TASK_MESH_PUSH_CONSTANTS_SIZE_DW \
   (BRW_TASK_MESH_INLINE_DATA_SIZE_DW - BRW_TASK_MESH_PUSH_CONSTANTS_START_DW)

/**
 * This enum is used as the base indice of the nir_load_topology_id_intel
 * intrinsic. This is used to return different values based on some aspect of
 * the topology of the device.
 */
enum brw_topology_id
{
   /* A value based of the DSS identifier the shader is currently running on.
    * Be mindful that the DSS ID can be higher than the total number of DSS on
    * the device. This is because of the fusing that can occur on different
    * parts.
    */
   BRW_TOPOLOGY_ID_DSS,

   /* A value composed of EU ID, thread ID & SIMD lane ID. */
   BRW_TOPOLOGY_ID_EU_THREAD_SIMD,
};

#ifdef __cplusplus
} /* extern "C" */
#endif
