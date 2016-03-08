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
#include "brw_device_info.h"
#include "main/mtypes.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ra_regs;
struct nir_shader;
struct brw_geometry_program;
union gl_constant_value;

struct brw_compiler {
   const struct brw_device_info *devinfo;

   struct {
      struct ra_regs *regs;

      /**
       * Array of the ra classes for the unaligned contiguous register
       * block sizes used.
       */
      int *classes;

      /**
       * Mapping for register-allocated objects in *regs to the first
       * GRF for that object.
       */
      uint8_t *ra_reg_to_grf;
   } vec4_reg_set;

   struct {
      struct ra_regs *regs;

      /**
       * Array of the ra classes for the unaligned contiguous register
       * block sizes used, indexed by register size.
       */
      int classes[16];

      /**
       * Mapping from classes to ra_reg ranges.  Each of the per-size
       * classes corresponds to a range of ra_reg nodes.  This array stores
       * those ranges in the form of first ra_reg in each class and the
       * total number of ra_reg elements in the last array element.  This
       * way the range of the i'th class is given by:
       * [ class_to_ra_reg_range[i], class_to_ra_reg_range[i+1] )
       */
      int class_to_ra_reg_range[17];

      /**
       * Mapping for register-allocated objects in *regs to the first
       * GRF for that object.
       */
      uint8_t *ra_reg_to_grf;

      /**
       * ra class for the aligned pairs we use for PLN, which doesn't
       * appear in *classes.
       */
      int aligned_pairs_class;
   } fs_reg_sets[2];

   void (*shader_debug_log)(void *, const char *str, ...) PRINTFLIKE(2, 3);
   void (*shader_perf_log)(void *, const char *str, ...) PRINTFLIKE(2, 3);

   bool scalar_stage[MESA_SHADER_STAGES];
   struct gl_shader_compiler_options glsl_compiler_options[MESA_SHADER_STAGES];
};


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

enum PACKED gen6_gather_sampler_wa {
   WA_SIGN = 1,      /* whether we need to sign extend */
   WA_8BIT = 2,      /* if we have an 8bit format needing wa */
   WA_16BIT = 4,     /* if we have a 16bit format needing wa */
};

/**
 * Sampler information needed by VS, WM, and GS program cache keys.
 */
struct brw_sampler_prog_key_data {
   /**
    * EXT_texture_swizzle and DEPTH_TEXTURE_MODE swizzles.
    */
   uint16_t swizzles[MAX_SAMPLERS];

   uint32_t gl_clamp_mask[3];

   /**
    * For RG32F, gather4's channel select is broken.
    */
   uint32_t gather_channel_quirk_mask;

   /**
    * Whether this sampler uses the compressed multisample surface layout.
    */
   uint32_t compressed_multisample_layout_mask;

   /**
    * Whether this sampler is using 16x multisampling. If so fetching from
    * this sampler will be handled with a different instruction, ld2dms_w
    * instead of ld2dms.
    */
   uint32_t msaa_16;

   /**
    * For Sandybridge, which shader w/a we need for gather quirks.
    */
   enum gen6_gather_sampler_wa gen6_gather_wa[MAX_SAMPLERS];
};


/** The program key for Vertex Shaders. */
struct brw_vs_prog_key {
   unsigned program_string_id;

   /*
    * Per-attribute workaround flags
    */
   uint8_t gl_attrib_wa_flags[VERT_ATTRIB_MAX];

   bool copy_edgeflag:1;

   bool clamp_vertex_color:1;

   /**
    * How many user clipping planes are being uploaded to the vertex shader as
    * push constants.
    *
    * These are used for lowering legacy gl_ClipVertex/gl_Position clipping to
    * clip distances.
    */
   unsigned nr_userclip_plane_consts:4;

   /**
    * For pre-Gen6 hardware, a bitfield indicating which texture coordinates
    * are going to be replaced with point coordinates (as a consequence of a
    * call to glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE)).  Because
    * our SF thread requires exact matching between VS outputs and FS inputs,
    * these texture coordinates will need to be unconditionally included in
    * the VUE, even if they aren't written by the vertex shader.
    */
   uint8_t point_coord_replace;

   struct brw_sampler_prog_key_data tex;
};

/** The program key for Tessellation Control Shaders. */
struct brw_tcs_prog_key
{
   unsigned program_string_id;

   GLenum tes_primitive_mode;

   unsigned input_vertices;

   /** A bitfield of per-patch outputs written. */
   uint32_t patch_outputs_written;

   /** A bitfield of per-vertex outputs written. */
   uint64_t outputs_written;

   struct brw_sampler_prog_key_data tex;
};

/** The program key for Tessellation Evaluation Shaders. */
struct brw_tes_prog_key
{
   unsigned program_string_id;

   /** A bitfield of per-patch inputs read. */
   uint32_t patch_inputs_read;

   /** A bitfield of per-vertex inputs read. */
   uint64_t inputs_read;

   struct brw_sampler_prog_key_data tex;
};

/** The program key for Geometry Shaders. */
struct brw_gs_prog_key
{
   unsigned program_string_id;

   struct brw_sampler_prog_key_data tex;
};

/** The program key for Fragment/Pixel Shaders. */
struct brw_wm_prog_key {
   uint8_t iz_lookup;
   bool stats_wm:1;
   bool flat_shade:1;
   bool persample_shading:1;
   bool persample_2x:1;
   unsigned nr_color_regions:5;
   bool replicate_alpha:1;
   bool render_to_fbo:1;
   bool clamp_fragment_color:1;
   bool compute_pos_offset:1;
   bool compute_sample_id:1;
   unsigned line_aa:2;
   bool high_quality_derivatives:1;
   bool force_dual_color_blend:1;

   uint16_t drawable_height;
   uint64_t input_slots_valid;
   unsigned program_string_id;
   GLenum alpha_test_func;          /* < For Gen4/5 MRT alpha test */
   float alpha_test_ref;

   struct brw_sampler_prog_key_data tex;
};

struct brw_cs_prog_key {
   uint32_t program_string_id;
   struct brw_sampler_prog_key_data tex;
};

/*
 * Image metadata structure as laid out in the shader parameter
 * buffer.  Entries have to be 16B-aligned for the vec4 back-end to be
 * able to use them.  That's okay because the padding and any unused
 * entries [most of them except when we're doing untyped surface
 * access] will be removed by the uniform packing pass.
 */
#define BRW_IMAGE_PARAM_SURFACE_IDX_OFFSET      0
#define BRW_IMAGE_PARAM_OFFSET_OFFSET           4
#define BRW_IMAGE_PARAM_SIZE_OFFSET             8
#define BRW_IMAGE_PARAM_STRIDE_OFFSET           12
#define BRW_IMAGE_PARAM_TILING_OFFSET           16
#define BRW_IMAGE_PARAM_SWIZZLING_OFFSET        20
#define BRW_IMAGE_PARAM_SIZE                    24

struct brw_image_param {
   /** Surface binding table index. */
   uint32_t surface_idx;

   /** Offset applied to the X and Y surface coordinates. */
   uint32_t offset[2];

   /** Surface X, Y and Z dimensions. */
   uint32_t size[3];

   /** X-stride in bytes, Y-stride in pixels, horizontal slice stride in
    * pixels, vertical slice stride in pixels.
    */
   uint32_t stride[4];

   /** Log2 of the tiling modulus in the X, Y and Z dimension. */
   uint32_t tiling[3];

   /**
    * Right shift to apply for bit 6 address swizzling.  Two different
    * swizzles can be specified and will be applied one after the other.  The
    * resulting address will be:
    *
    *  addr' = addr ^ ((1 << 6) & ((addr >> swizzling[0]) ^
    *                              (addr >> swizzling[1])))
    *
    * Use \c 0xff if any of the swizzles is not required.
    */
   uint32_t swizzling[2];
};

struct brw_stage_prog_data {
   struct {
      /** size of our binding table. */
      uint32_t size_bytes;

      /** @{
       * surface indices for the various groups of surfaces
       */
      uint32_t pull_constants_start;
      uint32_t texture_start;
      uint32_t gather_texture_start;
      uint32_t ubo_start;
      uint32_t ssbo_start;
      uint32_t abo_start;
      uint32_t image_start;
      uint32_t shader_time_start;
      /** @} */
   } binding_table;

   GLuint nr_params;       /**< number of float params/constants */
   GLuint nr_pull_params;
   unsigned nr_image_params;

   unsigned curb_read_length;
   unsigned total_scratch;
   unsigned total_shared;

   /**
    * Register where the thread expects to find input data from the URB
    * (typically uniforms, followed by vertex or fragment attributes).
    */
   unsigned dispatch_grf_start_reg;

   bool use_alt_mode; /**< Use ALT floating point mode?  Otherwise, IEEE. */

   /* Pointers to tracked values (only valid once
    * _mesa_load_state_parameters has been called at runtime).
    */
   const union gl_constant_value **param;
   const union gl_constant_value **pull_param;

   /** Image metadata passed to the shader as uniforms. */
   struct brw_image_param *image_param;
};

/* Data about a particular attempt to compile a program.  Note that
 * there can be many of these, each in a different GL state
 * corresponding to a different brw_wm_prog_key struct, with different
 * compiled programs.
 */
struct brw_wm_prog_data {
   struct brw_stage_prog_data base;

   GLuint num_varying_inputs;

   GLuint dispatch_grf_start_reg_16;
   GLuint reg_blocks;
   GLuint reg_blocks_16;

   struct {
      /** @{
       * surface indices the WM-specific surfaces
       */
      uint32_t render_target_start;
      /** @} */
   } binding_table;

   uint8_t computed_depth_mode;
   bool computed_stencil;

   bool early_fragment_tests;
   bool no_8;
   bool dual_src_blend;
   bool uses_pos_offset;
   bool uses_omask;
   bool uses_kill;
   bool uses_src_depth;
   bool uses_src_w;
   bool uses_sample_mask;
   bool pulls_bary;
   uint32_t prog_offset_16;

   /**
    * Mask of which interpolation modes are required by the fragment shader.
    * Used in hardware setup on gen6+.
    */
   uint32_t barycentric_interp_modes;

   /**
    * Map from gl_varying_slot to the position within the FS setup data
    * payload where the varying's attribute vertex deltas should be delivered.
    * For varying slots that are not used by the FS, the value is -1.
    */
   int urb_setup[VARYING_SLOT_MAX];
};

struct brw_cs_prog_data {
   struct brw_stage_prog_data base;

   GLuint dispatch_grf_start_reg_16;
   unsigned local_size[3];
   unsigned simd_size;
   bool uses_barrier;
   bool uses_num_work_groups;
   unsigned local_invocation_id_regs;

   struct {
      /** @{
       * surface indices the CS-specific surfaces
       */
      uint32_t work_groups_start;
      /** @} */
   } binding_table;
};

/**
 * Enum representing the i965-specific vertex results that don't correspond
 * exactly to any element of gl_varying_slot.  The values of this enum are
 * assigned such that they don't conflict with gl_varying_slot.
 */
typedef enum
{
   BRW_VARYING_SLOT_NDC = VARYING_SLOT_MAX,
   BRW_VARYING_SLOT_PAD,
   /**
    * Technically this is not a varying but just a placeholder that
    * compile_sf_prog() inserts into its VUE map to cause the gl_PointCoord
    * builtin variable to be compiled correctly. see compile_sf_prog() for
    * more info.
    */
   BRW_VARYING_SLOT_PNTC,
   BRW_VARYING_SLOT_COUNT
} brw_varying_slot;

/**
 * Data structure recording the relationship between the gl_varying_slot enum
 * and "slots" within the vertex URB entry (VUE).  A "slot" is defined as a
 * single octaword within the VUE (128 bits).
 *
 * Note that each BRW register contains 256 bits (2 octawords), so when
 * accessing the VUE in URB_NOSWIZZLE mode, each register corresponds to two
 * consecutive VUE slots.  When accessing the VUE in URB_INTERLEAVED mode (as
 * in a vertex shader), each register corresponds to a single VUE slot, since
 * it contains data for two separate vertices.
 */
struct brw_vue_map {
   /**
    * Bitfield representing all varying slots that are (a) stored in this VUE
    * map, and (b) actually written by the shader.  Does not include any of
    * the additional varying slots defined in brw_varying_slot.
    */
   GLbitfield64 slots_valid;

   /**
    * Is this VUE map for a separate shader pipeline?
    *
    * Separable programs (GL_ARB_separate_shader_objects) can be mixed and matched
    * without the linker having a chance to dead code eliminate unused varyings.
    *
    * This means that we have to use a fixed slot layout, based on the output's
    * location field, rather than assigning slots in a compact contiguous block.
    */
   bool separate;

   /**
    * Map from gl_varying_slot value to VUE slot.  For gl_varying_slots that are
    * not stored in a slot (because they are not written, or because
    * additional processing is applied before storing them in the VUE), the
    * value is -1.
    */
   signed char varying_to_slot[VARYING_SLOT_TESS_MAX];

   /**
    * Map from VUE slot to gl_varying_slot value.  For slots that do not
    * directly correspond to a gl_varying_slot, the value comes from
    * brw_varying_slot.
    *
    * For slots that are not in use, the value is BRW_VARYING_SLOT_PAD.
    */
   signed char slot_to_varying[VARYING_SLOT_TESS_MAX];

   /**
    * Total number of VUE slots in use
    */
   int num_slots;

   /**
    * Number of per-patch VUE slots. Only valid for tessellation control
    * shader outputs and tessellation evaluation shader inputs.
    */
   int num_per_patch_slots;

   /**
    * Number of per-vertex VUE slots. Only valid for tessellation control
    * shader outputs and tessellation evaluation shader inputs.
    */
   int num_per_vertex_slots;
};

void brw_print_vue_map(FILE *fp, const struct brw_vue_map *vue_map);

/**
 * Convert a VUE slot number into a byte offset within the VUE.
 */
static inline GLuint brw_vue_slot_to_offset(GLuint slot)
{
   return 16*slot;
}

/**
 * Convert a vertex output (brw_varying_slot) into a byte offset within the
 * VUE.
 */
static inline
GLuint brw_varying_to_offset(const struct brw_vue_map *vue_map, GLuint varying)
{
   return brw_vue_slot_to_offset(vue_map->varying_to_slot[varying]);
}

void brw_compute_vue_map(const struct brw_device_info *devinfo,
                         struct brw_vue_map *vue_map,
                         GLbitfield64 slots_valid,
                         bool separate_shader);

void brw_compute_tess_vue_map(struct brw_vue_map *const vue_map,
                              const GLbitfield64 slots_valid,
                              const GLbitfield is_patch);

enum shader_dispatch_mode {
   DISPATCH_MODE_4X1_SINGLE = 0,
   DISPATCH_MODE_4X2_DUAL_INSTANCE = 1,
   DISPATCH_MODE_4X2_DUAL_OBJECT = 2,
   DISPATCH_MODE_SIMD8 = 3,
};

/**
 * @defgroup Tessellator parameter enumerations.
 *
 * These correspond to the hardware values in 3DSTATE_TE, and are provided
 * as part of the tessellation evaluation shader.
 *
 * @{
 */
enum brw_tess_partitioning {
   BRW_TESS_PARTITIONING_INTEGER         = 0,
   BRW_TESS_PARTITIONING_ODD_FRACTIONAL  = 1,
   BRW_TESS_PARTITIONING_EVEN_FRACTIONAL = 2,
};

enum brw_tess_output_topology {
   BRW_TESS_OUTPUT_TOPOLOGY_POINT   = 0,
   BRW_TESS_OUTPUT_TOPOLOGY_LINE    = 1,
   BRW_TESS_OUTPUT_TOPOLOGY_TRI_CW  = 2,
   BRW_TESS_OUTPUT_TOPOLOGY_TRI_CCW = 3,
};

enum brw_tess_domain {
   BRW_TESS_DOMAIN_QUAD    = 0,
   BRW_TESS_DOMAIN_TRI     = 1,
   BRW_TESS_DOMAIN_ISOLINE = 2,
};
/** @} */

struct brw_vue_prog_data {
   struct brw_stage_prog_data base;
   struct brw_vue_map vue_map;

   /** Should the hardware deliver input VUE handles for URB pull loads? */
   bool include_vue_handles;

   GLuint urb_read_length;
   GLuint total_grf;

   /* Used for calculating urb partitions.  In the VS, this is the size of the
    * URB entry used for both input and output to the thread.  In the GS, this
    * is the size of the URB entry used for output.
    */
   GLuint urb_entry_size;

   enum shader_dispatch_mode dispatch_mode;
};

struct brw_vs_prog_data {
   struct brw_vue_prog_data base;

   GLbitfield64 inputs_read;

   unsigned nr_attributes;

   bool uses_vertexid;
   bool uses_instanceid;
   bool uses_basevertex;
   bool uses_baseinstance;
   bool uses_drawid;
};

struct brw_tcs_prog_data
{
   struct brw_vue_prog_data base;

   /** Number vertices in output patch */
   int instances;
};


struct brw_tes_prog_data
{
   struct brw_vue_prog_data base;

   enum brw_tess_partitioning partitioning;
   enum brw_tess_output_topology output_topology;
   enum brw_tess_domain domain;
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
    * Format of the control data (either GEN7_GS_CONTROL_DATA_FORMAT_GSCTL_SID
    * if the control data is StreamID bits, or
    * GEN7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT if the control data is cut bits).
    * Ignored if control_data_header_size is 0.
    */
   unsigned control_data_format;

   bool include_primitive_id;

   /**
    * The number of vertices emitted, if constant - otherwise -1.
    */
   int static_vertex_count;

   int invocations;

   /**
    * Gen6 transform feedback enabled flag.
    */
   bool gen6_xfb_enabled;

   /**
    * Gen6: Provoking vertex convention for odd-numbered triangles
    * in tristrips.
    */
   GLuint pv_first:1;

   /**
    * Gen6: Number of varyings that are output to transform feedback.
    */
   GLuint num_transform_feedback_bindings:7; /* 0-BRW_MAX_SOL_BINDINGS */

   /**
    * Gen6: Map from the index of a transform feedback binding table entry to the
    * gl_varying_slot that should be streamed out through that binding table
    * entry.
    */
   unsigned char transform_feedback_bindings[64 /* BRW_MAX_SOL_BINDINGS */];

   /**
    * Gen6: Map from the index of a transform feedback binding table entry to the
    * swizzles that should be used when streaming out data through that
    * binding table entry.
    */
   unsigned char transform_feedback_swizzles[64 /* BRW_MAX_SOL_BINDINGS */];
};


/** @} */

struct brw_compiler *
brw_compiler_create(void *mem_ctx, const struct brw_device_info *devinfo);

/**
 * Compile a vertex shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_vs(const struct brw_compiler *compiler, void *log_data,
               void *mem_ctx,
               const struct brw_vs_prog_key *key,
               struct brw_vs_prog_data *prog_data,
               const struct nir_shader *shader,
               gl_clip_plane *clip_planes,
               bool use_legacy_snorm_formula,
               int shader_time_index,
               unsigned *final_assembly_size,
               char **error_str);

/**
 * Compile a tessellation control shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_tcs(const struct brw_compiler *compiler,
                void *log_data,
                void *mem_ctx,
                const struct brw_tcs_prog_key *key,
                struct brw_tcs_prog_data *prog_data,
                const struct nir_shader *nir,
                int shader_time_index,
                unsigned *final_assembly_size,
                char **error_str);

/**
 * Compile a tessellation evaluation shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_tes(const struct brw_compiler *compiler, void *log_data,
                void *mem_ctx,
                const struct brw_tes_prog_key *key,
                struct brw_tes_prog_data *prog_data,
                const struct nir_shader *shader,
                struct gl_shader_program *shader_prog,
                int shader_time_index,
                unsigned *final_assembly_size,
                char **error_str);

/**
 * Compile a vertex shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_gs(const struct brw_compiler *compiler, void *log_data,
               void *mem_ctx,
               const struct brw_gs_prog_key *key,
               struct brw_gs_prog_data *prog_data,
               const struct nir_shader *shader,
               struct gl_shader_program *shader_prog,
               int shader_time_index,
               unsigned *final_assembly_size,
               char **error_str);

/**
 * Compile a fragment shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_fs(const struct brw_compiler *compiler, void *log_data,
               void *mem_ctx,
               const struct brw_wm_prog_key *key,
               struct brw_wm_prog_data *prog_data,
               const struct nir_shader *shader,
               struct gl_program *prog,
               int shader_time_index8,
               int shader_time_index16,
               bool use_rep_send,
               unsigned *final_assembly_size,
               char **error_str);

/**
 * Compile a compute shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_cs(const struct brw_compiler *compiler, void *log_data,
               void *mem_ctx,
               const struct brw_cs_prog_key *key,
               struct brw_cs_prog_data *prog_data,
               const struct nir_shader *shader,
               int shader_time_index,
               unsigned *final_assembly_size,
               char **error_str);

/**
 * Fill out local id payload for compute shader according to cs_prog_data.
 */
void
brw_cs_fill_local_id_payload(const struct brw_cs_prog_data *cs_prog_data,
                             void *buffer, uint32_t threads, uint32_t stride);

#ifdef __cplusplus
} /* extern "C" */
#endif
