/*
 * Copyright (C) 2025 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __PAN_COMPILER_H__
#define __PAN_COMPILER_H__

#include <stdbool.h>
#include <stdio.h>

#include "compiler/nir/nir_defines.h"
#include "compiler/shader_enums.h"
#include "util/u_dynarray.h"
#include "util/format/u_formats.h"
#include "util/shader_stats.h"

struct pan_shader_info;

const nir_shader_compiler_options *
pan_get_nir_shader_compiler_options(unsigned arch);

void pan_preprocess_nir(nir_shader *nir, unsigned gpu_id);
void pan_optimize_nir(nir_shader *nir, unsigned gpu_id);
void pan_postprocess_nir(nir_shader *nir, unsigned gpu_id);

#define PAN_PRINTF_BUFFER_SIZE 16384

/* Indices for named (non-XFB) varyings that are present. These are packed
 * tightly so they correspond to a bitfield present (P) indexed by (1 <<
 * PAN_VARY_*). This has the nice property that you can lookup the buffer index
 * of a given special field given a shift S by:
 *
 *      idx = popcount(P & ((1 << S) - 1))
 *
 * That is... look at all of the varyings that come earlier and count them, the
 * count is the new index since plus one. Likewise, the total number of special
 * buffers required is simply popcount(P)
 */

enum pan_special_varying {
   PAN_VARY_GENERAL = 0,
   PAN_VARY_POSITION = 1,
   PAN_VARY_PSIZ = 2,
   PAN_VARY_PNTCOORD = 3,
   PAN_VARY_FACE = 4,
   PAN_VARY_FRAGCOORD = 5,

   /* Keep last */
   PAN_VARY_MAX,
};

/* Maximum number of attribute descriptors required for varyings. These include
 * up to MAX_VARYING source level varyings plus a descriptor each non-GENERAL
 * special varying */
#define PAN_MAX_VARYINGS (MAX_VARYING + PAN_VARY_MAX - 1)

/* Special attribute slots for vertex builtins. Sort of arbitrary but let's be
 * consistent with the blob so we can compare traces easier. */

enum { PAN_VERTEX_ID = 16, PAN_INSTANCE_ID = 17, PAN_MAX_ATTRIBUTE };

/* Architecturally, Bifrost/Valhall can address 128 FAU slots of 64-bits each.
 * In practice, the maximum number of FAU slots is limited by implementation.
 * All known Bifrost and Valhall devices limit to 64 FAU slots. Therefore the
 * maximum number of 32-bit words is 128, since there are 2 words per FAU slot.
 *
 * Midgard can push at most 92 words, so this bound suffices. The Midgard
 * compiler pushes less than this, as Midgard uses register-mapped uniforms
 * instead of FAU, preventing large numbers of uniforms to be pushed for
 * nontrivial programs.
 */
#define PAN_MAX_PUSH 128

/* Architectural invariants (Midgard and Bifrost): UBO must be <= 2^16 bytes so
 * an offset to a word must be < 2^16. There are less than 2^8 UBOs */

struct pan_ubo_word {
   uint16_t ubo;
   uint16_t offset;
};

struct pan_ubo_push {
   unsigned count;
   struct pan_ubo_word words[PAN_MAX_PUSH];
};

/* Helper for searching the above. Note this is O(N) to the number of pushed
 * constants, do not run in the draw call hot path */

unsigned pan_lookup_pushed_ubo(struct pan_ubo_push *push, unsigned ubo,
                               unsigned offs);

struct pan_compile_inputs {
   unsigned gpu_id;
   uint32_t gpu_variant;
   /* Used on Bifrost and Valhall for pixel_local_storage load/store to convert
    * the format to a descriptor.
    */
   uint64_t (*get_conv_desc)(enum pipe_format fmt, unsigned rt,
                             unsigned force_size, bool dithered);
   bool is_blend, is_blit;
   struct {
      unsigned nr_samples;
      uint64_t bifrost_blend_desc;
   } blend;
   bool no_idvs;
   uint32_t view_mask;

   nir_variable_mode robust2_modes;
   /* Whether or not descriptor accesses should add additional robustness
    * checks. */
   bool robust_descriptors;

   /* Mask of UBOs that may be moved to push constants */
   uint32_t pushable_ubos;

   /* Used on Valhall.
    *
    * Bit mask of special desktop-only varyings (e.g VARYING_SLOT_TEX0)
    * written by the previous stage (fragment shader) or written by this
    * stage (vertex shader). Bits are slots from gl_varying_slot.
    *
    * For modern APIs (GLES or VK), this should be 0.
    */
   uint32_t fixed_varying_mask;

   /* Settings to move constants into the FAU. */
   struct {
      uint32_t *values;
      /* In multiples of 32bit. */
      uint32_t max_amount;
      /* In multiples of 32bit. */
      uint32_t offset;
   } fau_consts;

   union {
      struct {
         uint32_t rt_conv[8];
      } bifrost;
      struct {
         /* Use LD_VAR_BUF[_IMM] instead of LD_VAR[_IMM] to load varyings. */
         bool use_ld_var_buf;
      } valhall;
   };
};

struct pan_shader_varying {
   gl_varying_slot location;
   enum pipe_format format;
};

static inline unsigned
pan_get_fixed_varying_mask(unsigned varyings_used)
{
   return (varyings_used & BITFIELD_MASK(VARYING_SLOT_VAR0)) &
      ~VARYING_BIT_POS & ~VARYING_BIT_PSIZ;
}

struct bifrost_shader_blend_info {
   nir_alu_type type;
   uint32_t return_offset;
};

/*
 * Unpacked form of a v7 message preload descriptor, produced by the compiler's
 * message preload optimization. By splitting out this struct, the compiler does
 * not need to know about data structure packing, avoiding a dependency on
 * GenXML.
 */
struct bifrost_message_preload {
   /* Whether to preload this message */
   bool enabled;

   /* Varying to load from */
   unsigned varying_index;

   /* Register type, FP32 otherwise */
   bool fp16;

   /* Number of components, ignored if texturing */
   unsigned num_components;

   /* If texture is set, performs a texture instruction according to
    * texture_index, skip, and zero_lod. If texture is unset, only the
    * varying load is performed.
    */
   bool texture, skip, zero_lod;
   unsigned texture_index;
};

struct bifrost_shader_info {
   struct bifrost_shader_blend_info blend[8];
   nir_alu_type blend_src1_type;
   bool wait_6, wait_7;
   struct bifrost_message_preload messages[2];

   /* Whether any flat varyings are loaded. This may disable optimizations
    * that change the provoking vertex, since that would load incorrect
    * values for flat varyings.
    */
   bool uses_flat_shading;
};

struct midgard_shader_info {
   unsigned first_tag;
   union {
      struct {
         bool reads_raw_vertex_id;
      } vs;
   };
};

struct pan_shader_info {
   mesa_shader_stage stage;
   unsigned work_reg_count;
   unsigned tls_size;
   unsigned wls_size;

   struct pan_stats stats, stats_idvs_varying;

   /* Bit mask of preloaded registers */
   uint64_t preload;

   uint32_t fau_consts_count;
   uint32_t fau_consts[128];

   union {
      struct {
         bool reads_frag_coord;
         bool reads_point_coord;
         bool reads_primitive_id;
         bool reads_face;
         bool can_discard;
         bool writes_depth;
         bool writes_stencil;
         bool writes_coverage;
         bool sidefx;
         bool sample_shading;
         bool early_fragment_tests;
         bool can_early_z, can_fpk;
         bool untyped_color_outputs;
         uint32_t outputs_read;
      } fs;

      struct {
         bool writes_point_size;

         /* True if this shader needs the extended FIFO format for
          * more than just point size.
          */
         bool needs_extended_fifo;

         /* If the primary shader writes point size, the Valhall
          * driver may need a variant that does not write point
          * size. Offset to such a shader in the program binary.
          *
          * Zero if no such variant is required.
          *
          * Only used with IDVS on Valhall.
          */
         unsigned no_psiz_offset;

         /* Set if Index-Driven Vertex Shading is in use */
         bool idvs;

         /* If IDVS is used, whether a varying shader is used */
         bool secondary_enable;

         /* If a varying shader is used, the varying shader's
          * offset in the program binary
          */
         unsigned secondary_offset;

         /* If IDVS is in use, number of work registers used by
          * the varying shader
          */
         unsigned secondary_work_reg_count;

         /* If IDVS is in use, bit mask of preloaded registers
          * used by the varying shader
          */
         uint64_t secondary_preload;
      } vs;

      struct {
         /* Is it legal to merge workgroups? This is true if the
          * shader uses neither barriers nor shared memory. This
          * requires caution: if the API allows specifying shared
          * memory at launch time (instead of compile time), that
          * memory will not be accounted for by the compiler.
          *
          * Used by the Valhall hardware.
          */
         bool allow_merging_workgroups;
      } cs;
   };

   /* Does the shader contains a barrier? or (for fragment shaders) does it
    * require helper invocations, which demand the same ordering guarantees
    * of the hardware? These notions are unified in the hardware, so we
    * unify them here as well.
    */
   bool contains_barrier;
   bool separable;
   bool writes_global;
   uint64_t outputs_written;

   /* Floating point controls that the driver should try to honour */
   bool ftz_fp16, ftz_fp32;

   /* True if the shader contains a shader_clock instruction. */
   bool has_shader_clk_instr;

   unsigned sampler_count;
   unsigned texture_count;
   unsigned ubo_count;
   unsigned attributes_read_count;
   unsigned attribute_count;
   unsigned attributes_read;

   struct {
      unsigned input_count;
      struct pan_shader_varying input[PAN_MAX_VARYINGS];
      unsigned output_count;
      struct pan_shader_varying output[PAN_MAX_VARYINGS];

      /* Bitfield of noperspective varyings, starting at VARYING_SLOT_VAR0 */
      uint32_t noperspective;

      /* Bitfield of special varyings. */
      uint32_t fixed_varyings;
   } varyings;

   /* UBOs to push to Register Mapped Uniforms (Midgard) or Fast Access
    * Uniforms (Bifrost) */
   struct pan_ubo_push push;

   uint32_t ubo_mask;

   /* Quirk for GPUs that does not support auto32 types. */
   bool quirk_no_auto32;

   union {
      struct bifrost_shader_info bifrost;
      struct midgard_shader_info midgard;
   };
};

void pan_shader_compile(nir_shader *nir, struct pan_compile_inputs *inputs,
                        struct util_dynarray *binary,
                        struct pan_shader_info *info);

uint16_t pan_to_bytemask(unsigned bytes, unsigned mask);

/* NIR passes to do some backend-specific lowering */

#define PAN_WRITEOUT_C 1
#define PAN_WRITEOUT_Z 2
#define PAN_WRITEOUT_S 4
#define PAN_WRITEOUT_2 8

/*
 * Helper returning the subgroup size. Generally, this is equal to the number of
 * threads in a warp. For Midgard (including warping models), this returns 1, as
 * subgroups are not supported.
 */
static inline unsigned
pan_subgroup_size(unsigned arch)
{
   if (arch >= 9)
      return 16;
   else if (arch >= 7)
      return 8;
   else if (arch >= 6)
      return 4;
   else
      return 1;
}

/*
 * Helper extracting the table from a given handle of Valhall descriptor model.
 */
static inline unsigned
pan_res_handle_get_table(unsigned handle)
{
   unsigned table = handle >> 24;

   assert(table < 64);
   return table;
}

/*
 * Helper returning the index from a given handle of Valhall descriptor model.
 */
static inline unsigned
pan_res_handle_get_index(unsigned handle)
{
   return handle & BITFIELD_MASK(24);
}

/*
 * Helper creating an handle for Valhall descriptor model.
 */
static inline unsigned
pan_res_handle(unsigned table, unsigned index)
{
   assert(table < 64);
   assert(index < (1u << 24));

   return (table << 24) | index;
}

void pan_disassemble(FILE *fp, const void *code, size_t size,
                     unsigned gpu_id, bool verbose);

#endif /* __PAN_COMPILER_H__ */
