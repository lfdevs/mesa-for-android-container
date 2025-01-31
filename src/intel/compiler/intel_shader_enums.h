/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

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
};

struct intel_cs_dispatch_info {
   uint32_t group_size;
   uint32_t simd_size;
   uint32_t threads;

   /* RightExecutionMask field used in GPGPU_WALKER. */
   uint32_t right_mask;
};

enum PACKED intel_compute_walk_order {
   INTEL_WALK_ORDER_XYZ = 0,
   INTEL_WALK_ORDER_XZY = 1,
   INTEL_WALK_ORDER_YXZ = 2,
   INTEL_WALK_ORDER_YZX = 3,
   INTEL_WALK_ORDER_ZXY = 4,
   INTEL_WALK_ORDER_ZYX = 5,
};

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

   if (pushed_msaa_flags & INTEL_MSAA_FLAG_COARSE_RT_WRITES)
      assert(shader_coarse_pixel_dispatch != INTEL_NEVER);
   else
      assert(shader_coarse_pixel_dispatch != INTEL_ALWAYS);

   return (pushed_msaa_flags & INTEL_MSAA_FLAG_COARSE_RT_WRITES) != 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
