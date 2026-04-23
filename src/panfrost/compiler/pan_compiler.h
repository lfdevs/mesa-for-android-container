/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
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

bool pan_will_dump_shaders(unsigned arch);
bool pan_want_debug_info(unsigned arch);

const nir_shader_compiler_options *
pan_get_nir_shader_compiler_options(unsigned arch, bool merge_wg);

/* Inputs to the backend compiler */
struct pan_compile_inputs {
   uint64_t gpu_id;
   uint32_t gpu_variant;
   bool is_blend, is_blit;
   bool no_idvs;
   uint32_t view_mask;

   nir_variable_mode robust_modes;
   /* Whether or not descriptor accesses should add additional robustness
    * checks. */
   bool robust_descriptors;

   /* Mask of UBOs that may be moved to push constants */
   uint32_t pushable_ubos;

   /* Varying layout in memory, if known */
   const struct pan_varying_layout *varying_layout;

   /* Optimizations as nir_opt_varyings can erase all flat types to float, when
    * this field is false, varying types are inferred from their usage.
    */
   bool trust_varying_flat_highp_types;

   /* Settings to move constants into the FAU. */
   struct {
      uint32_t *values;
      /* In multiples of 32bit. */
      uint32_t max_amount;
      /* In multiples of 32bit. */
      uint32_t offset;
   } fau_consts;
};

/* Every panfrost compilation pipeline should adhere to:
 * 1. Driver-specific early lowering + pan_optimize_nir() (optional)
 * 2. pan_preprocess_nir()
 * 3. Descriptor lowering
 * 4. pan_postprocess_nir()
 * 5. Inline sysvals
 * 5. pan_shader_compile()
 * ONLY SYSVAL LOWERING is allowed between postprocess and shader_compile.
 * Driver-specific lowerings should be either BEFORE preprocess or BETWEEN
 * preprocess and postprocess.  Any code except sysval inlining put after
 * postprocess WILL BE NAKed.
 */
void pan_preprocess_nir(nir_shader *nir, uint64_t gpu_id);
void pan_optimize_nir(nir_shader *nir, uint64_t gpu_id);
void pan_postprocess_nir(nir_shader *nir, uint64_t gpu_id);

void pan_shader_compile(nir_shader *nir, struct pan_compile_inputs *inputs,
                        struct util_dynarray *binary,
                        struct pan_shader_info *info);

#define PAN_PRINTF_BUFFER_SIZE 16384

/* Any address with the top bit set is treated OOB by the hardware when
 * accessed from a shader and any reads will return zero and writes will be
 * discarded.  Using these is sometimes preferable to control-flow in the
 * shader.
 */
#define PAN_SHADER_OOB_ADDRESS (((uint64_t)1) << 63)

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

#define PAN_MAX_MULTIVIEW_VIEW_COUNT 8

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

enum pan_varying_section {
   PAN_VARYING_SECTION_POSITION,
   PAN_VARYING_SECTION_ATTRIBS,
   /* Varyings computed on-the-fly */
   PAN_VARYING_SECTION_SPECIAL,
   PAN_VARYING_SECTION_GENERIC,
};

/* Varyings which go in PAN_VARYING_SECTION_ATTRIBS */
#define PAN_ATTRIB_VARYING_BITS                                   \
   (VARYING_BIT_PSIZ | VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT | \
    VARYING_BIT_PRIMITIVE_ID)

/* Varyings which go in PAN_VARYING_SECTION_SPECIAL (Midgard only) */
#define PAN_SPECIAL_VARYING_BITS                                  \
   (VARYING_BIT_PNTC | VARYING_BIT_POS | VARYING_BIT_FACE)

/* Varyings which DO NOT go in PAN_VARYING_SECTION_GENERIC */
#define PAN_HARDWARE_VARYING_BITS                                  \
   (VARYING_BIT_POS | PAN_ATTRIB_VARYING_BITS | PAN_SPECIAL_VARYING_BITS)

struct pan_varying_slot {
   /* GLSL/SPIR-V location of the varying slot */
   gl_varying_slot location : 7;

   /* Format of the varying slot in memory
    * (really nir_alu_type, but the compiler screams at you if you don't lie) */
   unsigned alu_type : 8;
   unsigned ncomps : 3;

   enum pan_varying_section section : 2;

   /* Offset of the varying slot in the specified section of the varying
    * buffer.  For special VS outputs (see PAN_ATTRIB_VARYING_BITS), this is
    * relative to the start of the position header.  For all other varyings,
    * this is relative to the start of the varying space.  The offset will be
    * -1 if unknown (before the memory layout is built).
    */
   int offset : 12;
};
static_assert(sizeof(struct pan_varying_slot) == 4,
              "This struct has no holes");

static inline bool
pan_varying_slot_is_empty(const struct pan_varying_slot *slot)
{
   return slot->alu_type == nir_type_invalid;
}

enum ENUM_PACKED pan_varying_knowledge {
   PAN_VARYING_FORMAT_KNOWN = BITFIELD_BIT(0),
   PAN_VARYING_LAYOUT_KNOWN = BITFIELD_BIT(1),
};

/* Contains information about varyings, both their format and the physical
 * memory layout.  The format is not necessarily what is actually stored in
 * memory, but what format is in the register before the store_output, or what
 * the shader expects after a load_input.  The layout is optional and specifies
 * the exact offset in memory of each varying, its section and the size of the
 * generic buffer.  The layout is only built for the Vertex Shader and passed
 * on to the Fragment Shader if they are linked together, since the struct is
 * valid even without format or layout information, the "known" field tracks
 * what information the structure has, before accessing any format information
 * you should check with `pan_varying_layout_require_format` that it is built
 * and before accessing any layout information you should check with
 * pan_varying_layout_require_layout if it is present.
 *
 * The format and layout are not split into two different structures to avoid
 * duplicating indexing information.
 *
 * The slots are valid only up to `count`, but can also contain holes if they
 * have been dead-code-eliminated after `nir_assign_io_var_locations`.  Please
 * use `pan_varying_slot_is_empty` to check if slots are empty.  Empty slots are
 * ignored by finding functions.
 */
PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct pan_varying_layout {
   uint8_t count;
   enum pan_varying_knowledge known;
   /* Size of the generic section, in bytes */
   uint16_t generic_size_B;

   struct pan_varying_slot slots[PAN_MAX_VARYINGS];
};
PRAGMA_DIAGNOSTIC_POP

static inline const struct pan_varying_slot *
pan_varying_layout_find_slot(const struct pan_varying_layout *layout,
                             gl_varying_slot location)
{
   for (unsigned i = 0; i < layout->count; i++) {
      if (layout->slots[i].location != location)
         continue;
      const struct pan_varying_slot *slot = &layout->slots[i];
      if (pan_varying_slot_is_empty(slot))
         break;
      return slot;
   }

   return NULL;
}

static inline const struct pan_varying_slot *
pan_varying_layout_slot_at(const struct pan_varying_layout *layout,
                           unsigned index)
{
   if (index >= layout->count)
      return NULL;

   const struct pan_varying_slot *slot = &layout->slots[index];
   if (pan_varying_slot_is_empty(slot))
      return NULL;

   return slot;
}

static inline void
pan_varying_layout_require_format(const struct pan_varying_layout *layout)
{
   assert(layout);
   if (!(layout->known & PAN_VARYING_FORMAT_KNOWN))
      assert(!"Format is required");
}

static inline void
pan_varying_layout_require_layout(const struct pan_varying_layout *layout)
{
   assert(layout);
   if (!(layout->known & PAN_VARYING_LAYOUT_KNOWN))
      assert(!"Layout is required");
}

enum pipe_format
pan_varying_format(nir_alu_type type, unsigned ncomps);

void pan_build_varying_layout_compact(struct pan_varying_layout *layout,
                                      nir_shader *nir, uint64_t gpu_id);

void pan_varying_collect_formats(struct pan_varying_layout *registry,
                                 nir_shader *nir, uint64_t gpu_id,
                                 bool trust_varying_flat_highp_types,
                                 bool lower_mediump);

struct pan_shader_varying {
   gl_varying_slot location;
   enum pipe_format format;
};

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

   /* Whether there are any LD_VAR[_IMM] instructions that require varying
    * descriptors.  LD_VAR_BUF[_IMM] do not count as they only need buffer
    * descriptors to work correctly. For more details check out
    * docs/drivers/panfrost/varyings.rst
    */
   bool uses_ld_var;
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
         struct {
            bool ld_tile;
            bool wait_or_tile_access_before_atest_zsemit;
            bool rasterizer_coverage_read;
            bool centroid_interpolation;
            bool varying_before_atest_zsemit;
         } hsr;
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
   uint64_t images_used;

   struct {
      /* Bitfield of noperspective varyings, starting at VARYING_SLOT_VAR0 */
      uint32_t noperspective;

      struct pan_varying_layout formats;
   } varyings;

   /* UBOs to push to Register Mapped Uniforms (Midgard) or Fast Access
    * Uniforms (Bifrost) */
   struct pan_ubo_push push;

   uint32_t ubo_mask;

   union {
      struct bifrost_shader_info bifrost;
      struct midgard_shader_info midgard;
   };
};

void pan_shader_update_info(struct pan_shader_info *info, nir_shader *s,
                            const struct pan_compile_inputs *inputs);

uint16_t pan_to_bytemask(unsigned bytes, unsigned mask);

/* NIR passes to do some backend-specific lowering */

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
 * Helper returning the maximum offset in bytes (exclusive) that a LD_VAR_BUF*
 * instruction can use.
 */
static inline unsigned
pan_ld_var_buf_off_size(unsigned arch)
{
   if (arch >= 11)
      return 2048;
   else if (arch >= 9)
      return 256;
   else
      return 0;
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

void pan_disassemble(FILE *fp, const void *code, size_t size, uint64_t gpu_id,
                     bool verbose);

#endif /* __PAN_COMPILER_H__ */
