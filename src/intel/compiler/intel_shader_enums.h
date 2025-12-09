/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef __OPENCL_VERSION__
#include <stdint.h>
#include "util/bitscan.h"
#endif

#include "compiler/shader_enums.h"
#include "util/enum_operators.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A tri-state value to track states that are potentially dynamic */
enum intel_sometimes {
   INTEL_NEVER = 0,
   INTEL_SOMETIMES,
   INTEL_ALWAYS
};

static inline enum intel_sometimes
intel_sometimes_invert(enum intel_sometimes x)
{
   return (enum intel_sometimes)((int)INTEL_ALWAYS - (int)x);
}

#define INTEL_TESS_CONFIG_INPUT_VERTICES_OFFSET   (0)
#define INTEL_TESS_CONFIG_INPUT_VERTICES_SIZE     (5)
#define INTEL_TESS_CONFIG_OUTPUT_VERTICES_OFFSET  (5)
#define INTEL_TESS_CONFIG_OUTPUT_VERTICES_SIZE    (5)
#define INTEL_TESS_CONFIG_BUILTINS_OFFSET         (10)
#define INTEL_TESS_CONFIG_BUILTINS_SIZE           (6)
#define INTEL_TESS_CONFIG_PER_VERTEX_SLOTS_OFFSET (16)
#define INTEL_TESS_CONFIG_PER_VERTEX_SLOTS_SIZE   (6)
#define INTEL_TESS_CONFIG_PER_PATCH_SLOTS_OFFSET  (22)
#define INTEL_TESS_CONFIG_PER_PATCH_SLOTS_SIZE    (6)

enum intel_tess_configs {
   /** Tessellation inputs vertices minus 1
    *
    * This field actually covers 5bits.
    */
   INTEL_TESS_CONFIG_INPUT_VERTICES  = BITFIELD_BIT(INTEL_TESS_CONFIG_INPUT_VERTICES_OFFSET),

   /** Tessellation outputs vertices minus 1
    *
    * This field actually covers 5bits.
    */
   INTEL_TESS_CONFIG_OUTPUT_VERTICES = BITFIELD_BIT(INTEL_TESS_CONFIG_OUTPUT_VERTICES_OFFSET),

   /** Tessellation builtins per-vertex offset
    *
    * This field actually covers 5bits.
    */
   INTEL_TESS_CONFIG_BUILTINS      = BITFIELD_BIT(INTEL_TESS_CONFIG_BUILTINS_OFFSET),

   /** Number of per vertex slots
    *
    * This field actually covers 6bits.
    */
   INTEL_TESS_PER_VERTEX_SLOTS     = BITFIELD_BIT(INTEL_TESS_CONFIG_PER_VERTEX_SLOTS_OFFSET),

   /** Number of per patch slots
    *
    * This field actually covers 6bits.
    */
   INTEL_TESS_PER_PATCH_SLOTS      = BITFIELD_BIT(INTEL_TESS_CONFIG_PER_PATCH_SLOTS_OFFSET),

   /** Tesselation primitive modes
    *
    * Only one of the following 3 bits should be set.
    */
   INTEL_TESS_CONFIG_QUADS           = BITFIELD_BIT(29),
   INTEL_TESS_CONFIG_TRIANGLES       = BITFIELD_BIT(30),
   INTEL_TESS_CONFIG_ISOLINES        = BITFIELD_BIT(31)
};

#define INTEL_MSAA_FLAG_FIRST_VUE_SLOT_OFFSET     (19)
#define INTEL_MSAA_FLAG_FIRST_VUE_SLOT_SIZE       (6)
#define INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_OFFSET (25)
#define INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_SIZE   (6)
#define INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_MESH   (32)

enum intel_msaa_flags {
   /** Must be set whenever any dynamic MSAA is used
    *
    * This flag mostly exists to let us assert that the driver understands
    * dynamic MSAA so we don't run into trouble with drivers that don't.
    */
   INTEL_MSAA_FLAG_ENABLE_DYNAMIC = (1 << 0),

   /** True if the framebuffer is multisampled */
   INTEL_MSAA_FLAG_MULTISAMPLE_FBO = (1 << 1),

   /** True if this shader has been dispatched per-sample */
   INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH = (1 << 2),

   /** True if inputs should be interpolated per-sample by default */
   INTEL_MSAA_FLAG_PERSAMPLE_INTERP = (1 << 3),

   /** True if this shader has been dispatched with alpha-to-coverage */
   INTEL_MSAA_FLAG_ALPHA_TO_COVERAGE = (1 << 4),

   /** True if provoking vertex is last */
   INTEL_MSAA_FLAG_PROVOKING_VERTEX_LAST = (1 << 5),

   /** True if we need to apply Wa_18019110168 remapping */
   INTEL_MSAA_FLAG_PER_PRIMITIVE_REMAPPING = (1 << 6),

   /** True if this shader has been dispatched coarse
    *
    * This is intentionally chose to be bit 15 to correspond to the coarse bit
    * in the pixel interpolator messages.
    */
   INTEL_MSAA_FLAG_COARSE_PI_MSG = (1 << 15),

   /** True if this shader has been dispatched coarse
    *
    * This is intentionally chose to be bit 18 to correspond to the coarse bit
    * in the render target messages.
    */
   INTEL_MSAA_FLAG_COARSE_RT_WRITES = (1 << 18),

   /** First slot read in the VUE
    *
    * This is not a flag but a value that cover 6bits.
    */
   INTEL_MSAA_FLAG_FIRST_VUE_SLOT = (1 << INTEL_MSAA_FLAG_FIRST_VUE_SLOT_OFFSET),

   /** Index of the PrimitiveID attribute relative to the first read
    * attribute.
    *
    * This is not a flag but a value that cover 6bits. Value 32 means the
    * PrimitiveID is coming from the PerPrimitive block, written by the Mesh
    * shader.
    */
   INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX = (1 << INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_OFFSET),
};
MESA_DEFINE_CPP_ENUM_BITFIELD_OPERATORS(intel_msaa_flags)

/**
 * @defgroup Tessellator parameter enumerations.
 *
 * These correspond to the hardware values in 3DSTATE_TE, and are provided
 * as part of the tessellation evaluation shader.
 *
 * @{
 */
enum intel_tess_partitioning {
   INTEL_TESS_PARTITIONING_INTEGER         = 0,
   INTEL_TESS_PARTITIONING_ODD_FRACTIONAL  = 1,
   INTEL_TESS_PARTITIONING_EVEN_FRACTIONAL = 2,
};

enum intel_tess_output_topology {
   INTEL_TESS_OUTPUT_TOPOLOGY_POINT   = 0,
   INTEL_TESS_OUTPUT_TOPOLOGY_LINE    = 1,
   INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CW  = 2,
   INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CCW = 3,
};

enum intel_tess_domain {
   INTEL_TESS_DOMAIN_QUAD    = 0,
   INTEL_TESS_DOMAIN_TRI     = 1,
   INTEL_TESS_DOMAIN_ISOLINE = 2,
};
/** @} */

enum intel_shader_dispatch_mode {
   INTEL_DISPATCH_MODE_4X1_SINGLE = 0,
   INTEL_DISPATCH_MODE_4X2_DUAL_INSTANCE = 1,
   INTEL_DISPATCH_MODE_4X2_DUAL_OBJECT = 2,
   INTEL_DISPATCH_MODE_SIMD8 = 3,

   INTEL_DISPATCH_MODE_TCS_SINGLE_PATCH = 0,
   INTEL_DISPATCH_MODE_TCS_MULTI_PATCH = 2,
};

enum intel_barycentric_mode {
   INTEL_BARYCENTRIC_PERSPECTIVE_PIXEL       = 0,
   INTEL_BARYCENTRIC_PERSPECTIVE_CENTROID    = 1,
   INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE      = 2,
   INTEL_BARYCENTRIC_NONPERSPECTIVE_PIXEL    = 3,
   INTEL_BARYCENTRIC_NONPERSPECTIVE_CENTROID = 4,
   INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE   = 5,
   INTEL_BARYCENTRIC_MODE_COUNT              = 6
};
#define INTEL_BARYCENTRIC_PERSPECTIVE_BITS \
   ((1 << INTEL_BARYCENTRIC_PERSPECTIVE_PIXEL) | \
    (1 << INTEL_BARYCENTRIC_PERSPECTIVE_CENTROID) | \
    (1 << INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE))
#define INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS \
   ((1 << INTEL_BARYCENTRIC_NONPERSPECTIVE_PIXEL) | \
    (1 << INTEL_BARYCENTRIC_NONPERSPECTIVE_CENTROID) | \
    (1 << INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE))

enum intel_vue_layout {
   /**
    * Layout is fixed and shared by producer/consumer, allowing for tigh
    * packing
    */
   INTEL_VUE_LAYOUT_FIXED = 0,
   /**
    * Layout is separate, works for ARB_separate_shader_objects but without
    * Mesh support.
    */
   INTEL_VUE_LAYOUT_SEPARATE,
   /**
    * Layout is separate and works with Mesh shaders.
    */
   INTEL_VUE_LAYOUT_SEPARATE_MESH,
};

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
struct intel_vue_map {
   /**
    * Bitfield representing all varying slots that are (a) stored in this VUE
    * map, and (b) actually written by the shader.  Does not include any of
    * the additional varying slots defined in brw_varying_slot.
    */
   uint64_t slots_valid;

   /**
    * The layout of the VUE
    *
    * Separable programs (GL_ARB_separate_shader_objects) can be mixed and
    * matched without the linker having a chance to dead code eliminate unused
    * varyings.
    *
    * This means that we have to use a fixed slot layout, based on the output's
    * location field, rather than assigning slots in a compact contiguous block.
    *
    * When using Mesh, another constraint arises which is the HW limits for
    * loading per-primitive & per-vertex data, limited to 32 varying in total.
    * This requires us to be quite inventive with the way we lay things out.
    * Take a fragment shader loading the following data :
    *
    *    float gl_ClipDistance[];
    *    uint gl_PrimitiveID;
    *    vec4 someAppValue[29];
    *
    * According to the Vulkan spec, someAppValue will occupy 29 slots,
    * gl_PrimitiveID 1 slot, gl_ClipDistance[] up to 2 slots. If the input is
    * coming from a VS/DS/GS shader, we can load all of this through a single
    * block using 3DSTATE_SBE::VertexURBEntryReadLength = 16 (maximum
    * programmable value) and the layout with
    * BRW_VUE_MAP_LAYOUT_FIXED/BRW_VUE_MAP_LAYOUT_SEPARATE will be this :
    *
    *   -----------------------
    *   | gl_ClipDistance 0-3 |
    *   |---------------------|
    *   | gl_ClipDistance 4-7 |
    *   |---------------------|
    *   |   gl_PrimitiveID    |
    *   |---------------------|
    *   |   someAppValue[]    |
    *   |---------------------|
    *
    * This works nicely as everything is coming from the same location in the
    * URB.
    *
    * When mesh shaders are involved, gl_PrimitiveID is located in a different
    * place in the URB (the per-primitive block) and requires programming
    * 3DSTATE_SBE_MESH::PerPrimitiveURBEntryOutputReadLength to load some
    * additional data. The HW has a limit such that
    * 3DSTATE_SBE_MESH::PerPrimitiveURBEntryOutputReadLength +
    * 3DSTATE_SBE_MESH::PerVertexURBEntryOutputReadLength <= 16. With the
    * layout above, we would not be able to accomodate that HW limit.
    *
    * The solution to this is to lay the built-in varyings out
    * (gl_ClipDistance omitted since it's part of the VUE header and cannot
    * live any other place) at the end of the VUE like this :
    *
    *   -----------------------
    *   | gl_ClipDistance 0-3 |
    *   |---------------------|
    *   | gl_ClipDistance 4-7 |
    *   |---------------------|
    *   |   someAppValue[]    |
    *   |---------------------|
    *   |   gl_PrimitiveID    |
    *   |---------------------|
    *
    * This layout adds another challenge because with separate shader
    * compilations, we cannot tell in the consumer shader how many outputs the
    * producer has, so we don't know where the gl_PrimitiveID lives. The
    * solution to this other problem is to read the built-in with a
    * MOV_INDIRECT and have the offset of the MOV_INDIRECT loaded through a
    * push constant.
    */
   enum intel_vue_layout layout;

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
    * Number of position VUE slots.  If num_pos_slots > 1, primitive
    * replication is being used.
    */
   int num_pos_slots;

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

   /**
    * Location at which the builtins live.
    */
   int builtins_slot_offset;
};

struct intel_cs_dispatch_info {
   uint32_t group_size;
   uint32_t simd_size;
   uint32_t threads;

   /* RightExecutionMask field used in GPGPU_WALKER. */
   uint32_t right_mask;
};

enum intel_compute_walk_order {
   INTEL_WALK_ORDER_XYZ = 0,
   INTEL_WALK_ORDER_XZY = 1,
   INTEL_WALK_ORDER_YXZ = 2,
   INTEL_WALK_ORDER_YZX = 3,
   INTEL_WALK_ORDER_ZXY = 4,
   INTEL_WALK_ORDER_ZYX = 5,
};

static inline uint32_t
intel_tess_config(uint32_t input_vertices,
                  uint32_t output_vertices,
                  enum intel_tess_domain tess_domain,
                  uint32_t num_per_patch_slots,
                  uint32_t num_per_vertex_slots,
                  uint32_t builtins_slot_offset)
{
   assert(num_per_patch_slots < (1u << INTEL_TESS_CONFIG_PER_PATCH_SLOTS_SIZE));
   assert(num_per_vertex_slots < (1u << INTEL_TESS_CONFIG_PER_VERTEX_SLOTS_SIZE));
   assert(builtins_slot_offset < (1u << INTEL_TESS_CONFIG_BUILTINS_SIZE));
   assert(input_vertices != 0);
   assert((input_vertices - 1) < (1u << INTEL_TESS_CONFIG_INPUT_VERTICES_SIZE));
   assert((output_vertices - 1) < (1u << INTEL_TESS_CONFIG_OUTPUT_VERTICES_SIZE));

   const uint32_t primitive_flags =
      tess_domain == INTEL_TESS_DOMAIN_TRI ? INTEL_TESS_CONFIG_TRIANGLES :
      tess_domain == INTEL_TESS_DOMAIN_QUAD ? INTEL_TESS_CONFIG_QUADS :
      INTEL_TESS_CONFIG_ISOLINES;

   return
      (((input_vertices - 1) & 0x1f) << INTEL_TESS_CONFIG_INPUT_VERTICES_OFFSET) |
      (((output_vertices - 1) & 0x1f) << INTEL_TESS_CONFIG_OUTPUT_VERTICES_OFFSET) |
      primitive_flags |
      (num_per_patch_slots << INTEL_TESS_CONFIG_PER_PATCH_SLOTS_OFFSET) |
      (num_per_vertex_slots << INTEL_TESS_CONFIG_PER_VERTEX_SLOTS_OFFSET) |
      (builtins_slot_offset << INTEL_TESS_CONFIG_BUILTINS_OFFSET);

}

static inline bool
intel_fs_is_persample(enum intel_sometimes shader_persample_dispatch,
                      bool shader_per_sample_shading,
                      enum intel_msaa_flags pushed_msaa_flags)
{
   if (shader_persample_dispatch != INTEL_SOMETIMES)
      return shader_persample_dispatch;

   assert(pushed_msaa_flags & INTEL_MSAA_FLAG_ENABLE_DYNAMIC);

   if (!(pushed_msaa_flags & INTEL_MSAA_FLAG_MULTISAMPLE_FBO))
      return false;

   if (shader_per_sample_shading)
      assert(pushed_msaa_flags & INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH);

   return (pushed_msaa_flags & INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH) != 0;
}

static inline uint32_t
intel_fs_barycentric_modes(enum intel_sometimes shader_persample_dispatch,
                           uint32_t shader_barycentric_modes,
                           enum intel_msaa_flags pushed_msaa_flags)
{
   /* In the non dynamic case, we can just return the computed shader_barycentric_modes from
    * compilation time.
    */
   if (shader_persample_dispatch != INTEL_SOMETIMES)
      return shader_barycentric_modes;

   uint32_t modes = shader_barycentric_modes;

   assert(pushed_msaa_flags & INTEL_MSAA_FLAG_ENABLE_DYNAMIC);

   if (pushed_msaa_flags & INTEL_MSAA_FLAG_PERSAMPLE_INTERP) {
      assert(pushed_msaa_flags & INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH);

      /* Making dynamic per-sample interpolation work is a bit tricky.  The
       * hardware will hang if SAMPLE is requested but per-sample dispatch is
       * not enabled.  This means we can't preemptively add SAMPLE to the
       * barycentrics bitfield.  Instead, we have to add it late and only
       * on-demand.  Annoyingly, changing the number of barycentrics requested
       * changes the whole PS shader payload so we very much don't want to do
       * that.  Instead, if the dynamic per-sample interpolation flag is set,
       * we check to see if SAMPLE was requested and, if not, replace the
       * highest barycentric bit in the [non]perspective grouping (CENTROID,
       * if it exists, else PIXEL) with SAMPLE.  The shader will stomp all the
       * barycentrics in the shader with SAMPLE so it really doesn't matter
       * which one we replace.  The important thing is that we keep the number
       * of barycentrics in each [non]perspective grouping the same.
       */
      if ((modes & INTEL_BARYCENTRIC_PERSPECTIVE_BITS) &&
          !(modes & BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE))) {
         int sample_mode =
            util_last_bit(modes & INTEL_BARYCENTRIC_PERSPECTIVE_BITS) - 1;
         assert(modes & BITFIELD_BIT(sample_mode));

         modes &= ~BITFIELD_BIT(sample_mode);
         modes |= BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE);
      }

      if ((modes & INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS) &&
          !(modes & BITFIELD_BIT(INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE))) {
         int sample_mode =
            util_last_bit(modes & INTEL_BARYCENTRIC_NONPERSPECTIVE_BITS) - 1;
         assert(modes & BITFIELD_BIT(sample_mode));

         modes &= ~BITFIELD_BIT(sample_mode);
         modes |= BITFIELD_BIT(INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE);
      }
   } else {
      /* If we're not using per-sample interpolation, we need to disable the
       * per-sample bits.
       *
       * SKL PRMs, Volume 2a: Command Reference: Instructions,
       * 3DSTATE_WM:Barycentric Interpolation Mode:

       *    "MSDISPMODE_PERSAMPLE is required in order to select Perspective
       *     Sample or Non-perspective Sample barycentric coordinates."
       */
      uint32_t sample_bits = (BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE) |
                              BITFIELD_BIT(INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE));
      uint32_t requested_sample = modes & sample_bits;
      modes &= ~sample_bits;
      /*
       * If the shader requested some sample modes and we have to disable
       * them, make sure we add back the pixel variant back to not mess up the
       * thread payload.
       *
       * Why does this works out? Because of the ordering in the thread payload :
       *
       *   R7:10  Perspective Centroid Barycentric
       *   R11:14 Perspective Sample Barycentric
       *   R15:18 Linear Pixel Location Barycentric
       *
       * In the backend when persample dispatch is dynamic, we always select
       * the sample barycentric and turn off the pixel location (even if
       * requested through intrinsics). That way when we dynamically select
       * pixel or sample dispatch, the barycentric always match, since the
       * pixel location barycentric register offset will align with the sample
       * barycentric.
       */
      if (requested_sample) {
         if (requested_sample & BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE))
            modes |= BITFIELD_BIT(INTEL_BARYCENTRIC_PERSPECTIVE_PIXEL);
         if (requested_sample & BITFIELD_BIT(INTEL_BARYCENTRIC_NONPERSPECTIVE_SAMPLE))
            modes |= BITFIELD_BIT(INTEL_BARYCENTRIC_NONPERSPECTIVE_PIXEL);
      }
   }

   return modes;
}


static inline bool
intel_fs_is_coarse(enum intel_sometimes shader_coarse_pixel_dispatch,
                   enum intel_msaa_flags pushed_msaa_flags)
{
   if (shader_coarse_pixel_dispatch != INTEL_SOMETIMES)
      return shader_coarse_pixel_dispatch;

   assert(pushed_msaa_flags & INTEL_MSAA_FLAG_ENABLE_DYNAMIC);

   assert((pushed_msaa_flags & INTEL_MSAA_FLAG_COARSE_RT_WRITES) ?
          shader_coarse_pixel_dispatch != INTEL_NEVER :
          shader_coarse_pixel_dispatch != INTEL_ALWAYS);

   return (pushed_msaa_flags & INTEL_MSAA_FLAG_COARSE_RT_WRITES) != 0;
}

struct intel_fs_params {
   bool shader_sample_shading;
   float shader_min_sample_shading;
   bool state_sample_shading;
   uint32_t rasterization_samples;
   bool coarse_pixel;
   bool alpha_to_coverage;
   bool provoking_vertex_last;
   uint32_t first_vue_slot;
   uint32_t primitive_id_index;
   bool per_primitive_remapping;
};

static inline enum intel_msaa_flags
intel_fs_msaa_flags(struct intel_fs_params params)
{
   enum intel_msaa_flags fs_msaa_flags = INTEL_MSAA_FLAG_ENABLE_DYNAMIC;

   if (params.rasterization_samples > 1) {
      fs_msaa_flags |= INTEL_MSAA_FLAG_MULTISAMPLE_FBO;

      if (params.shader_sample_shading)
         fs_msaa_flags |= INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH;

      if (params.shader_sample_shading ||
          (params.state_sample_shading &&
           (params.shader_min_sample_shading *
            params.rasterization_samples) > 1)) {
         fs_msaa_flags |= INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH |
                          INTEL_MSAA_FLAG_PERSAMPLE_INTERP;
      }
   }

   if (!(fs_msaa_flags & INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH) &&
       params.coarse_pixel) {
      fs_msaa_flags |= INTEL_MSAA_FLAG_COARSE_PI_MSG |
                       INTEL_MSAA_FLAG_COARSE_RT_WRITES;
   }

   if (params.alpha_to_coverage)
      fs_msaa_flags |= INTEL_MSAA_FLAG_ALPHA_TO_COVERAGE;

   assert(params.first_vue_slot < (1 << INTEL_MSAA_FLAG_FIRST_VUE_SLOT_SIZE));
   fs_msaa_flags |= (enum intel_msaa_flags)(
      params.first_vue_slot << INTEL_MSAA_FLAG_FIRST_VUE_SLOT_OFFSET);

   assert(params.primitive_id_index < (1u << INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_SIZE));
   fs_msaa_flags |= (enum intel_msaa_flags)(
      params.primitive_id_index << INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_OFFSET);

   if (params.provoking_vertex_last)
      fs_msaa_flags |= INTEL_MSAA_FLAG_PROVOKING_VERTEX_LAST;

   if (params.per_primitive_remapping)
      fs_msaa_flags |= INTEL_MSAA_FLAG_PER_PRIMITIVE_REMAPPING;

   return fs_msaa_flags;
}

#define INTEL_MAX_EMBEDDED_SAMPLERS (4096)

enum intel_shader_reloc_id {
   INTEL_SHADER_RELOC_CONST_DATA_ADDR_LOW,
   INTEL_SHADER_RELOC_CONST_DATA_ADDR_HIGH,
   INTEL_SHADER_RELOC_SHADER_START_OFFSET,

   INTEL_LAST_COMMON_SHADER_RELOC = INTEL_SHADER_RELOC_SHADER_START_OFFSET,

   BRW_SHADER_RELOC_RESUME_SBT_ADDR_LOW = INTEL_LAST_COMMON_SHADER_RELOC + 1,
   BRW_SHADER_RELOC_RESUME_SBT_ADDR_HIGH,
   BRW_SHADER_RELOC_DESCRIPTORS_ADDR_HIGH,
   BRW_SHADER_RELOC_DESCRIPTORS_BUFFER_ADDR_HIGH,
   BRW_SHADER_RELOC_INSTRUCTION_BASE_ADDR_HIGH,
   BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_LOW,
   BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_HIGH,
   BRW_SHADER_RELOC_PRINTF_BUFFER_SIZE,
   /* Leave this entry last: */
   BRW_SHADER_RELOC_EMBEDDED_SAMPLER_HANDLE,
   BRW_SHADER_RELOC_LAST_EMBEDDED_SAMPLER_HANDLE =
   BRW_SHADER_RELOC_EMBEDDED_SAMPLER_HANDLE + INTEL_MAX_EMBEDDED_SAMPLERS - 1,
};

enum intel_shader_reloc_type {
   /** An arbitrary 32-bit value */
   INTEL_SHADER_RELOC_TYPE_U32,
   /** A MOV instruction with an immediate source */
   INTEL_SHADER_RELOC_TYPE_MOV_IMM,
};

/** Represents a code relocation
 *
 * Relocatable constants are immediates in the code which we want to be able
 * to replace post-compile with the actual value.
 */
struct intel_shader_reloc {
   /** The 32-bit ID of the relocatable constant */
   uint32_t id;

   /** Type of this relocation */
   enum intel_shader_reloc_type type;

   /** The offset in the shader to the relocated value
    *
    * For MOV_IMM relocs, this is an offset to the MOV instruction.  This
    * allows us to do some sanity checking while we update the value.
    */
   uint32_t offset;

   /** Value to be added to the relocated value before it is written */
   uint32_t delta;
};

/** A value to write to a relocation */
struct intel_shader_reloc_value {
   /** The 32-bit ID of the relocatable constant */
   uint32_t id;

   /** The value with which to replace the relocated immediate */
   uint32_t value;
};

#ifdef __cplusplus
} /* extern "C" */
#endif
