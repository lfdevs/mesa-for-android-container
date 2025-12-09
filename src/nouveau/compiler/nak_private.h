/*
 * Copyright © 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef NAK_PRIVATE_H
#define NAK_PRIVATE_H

#include "nak.h"
#include "nir.h"
#include "nv_device_info.h"

#ifdef __cplusplus
extern "C" {
#endif

bool nak_should_print_nir(void);
bool nak_debug_no_ugpr(void);

struct nak_compiler {
   uint8_t sm;
   uint8_t warps_per_sm;

   struct nir_shader_compiler_options nir_options;
};

enum ENUM_PACKED nak_attr {
   /* System values A */
   NAK_ATTR_TESS_LOD_LEFT     = 0x000,
   NAK_ATTR_TESS_LOD_RIGHT    = 0x004,
   NAK_ATTR_TESS_LOD_BOTTOM   = 0x008,
   NAK_ATTR_TESS_LOD_TOP      = 0x00c,
   NAK_ATTR_TESS_LOD          = NAK_ATTR_TESS_LOD_LEFT,
   NAK_ATTR_TESS_INTERRIOR_U  = 0x010,
   NAK_ATTR_TESS_INTERRIOR_V  = 0x014,
   NAK_ATTR_TESS_INTERRIOR    = NAK_ATTR_TESS_INTERRIOR_U,

   /* Patch attributes */
   NAK_ATTR_PATCH_START       = 0x020,

   /* System values B? */
   NAK_ATTR_VPRS_TABLE_INDEX  = 0x5c,

   /* System values B */
   NAK_ATTR_PRIMITIVE_ID      = 0x060,
   NAK_ATTR_RT_ARRAY_INDEX    = 0x064,
   NAK_ATTR_VIEWPORT_INDEX    = 0x068,
   NAK_ATTR_POINT_SIZE        = 0x06c,
   NAK_ATTR_POSITION_X        = 0x070,
   NAK_ATTR_POSITION_Y        = 0x074,
   NAK_ATTR_POSITION_Z        = 0x078,
   NAK_ATTR_POSITION_W        = 0x07c,
   NAK_ATTR_POSITION          = NAK_ATTR_POSITION_X,

   /* Generic attributes */
   NAK_ATTR_GENERIC_START     = 0x080,

   /* System values C */
   NAK_ATTR_CLIP_CULL_DIST_0  = 0x2c0,
   NAK_ATTR_CLIP_CULL_DIST_1  = 0x2c4,
   NAK_ATTR_CLIP_CULL_DIST_2  = 0x2c8,
   NAK_ATTR_CLIP_CULL_DIST_3  = 0x2cc,
   NAK_ATTR_CLIP_CULL_DIST_4  = 0x2d0,
   NAK_ATTR_CLIP_CULL_DIST_5  = 0x2d4,
   NAK_ATTR_CLIP_CULL_DIST_6  = 0x2d8,
   NAK_ATTR_CLIP_CULL_DIST_7  = 0x2dc,
   NAK_ATTR_CLIP_CULL_DIST    = NAK_ATTR_CLIP_CULL_DIST_0,
   NAK_ATTR_POINT_SPRITE_S    = 0x2e0,
   NAK_ATTR_POINT_SPRITE_T    = 0x2e4,
   NAK_ATTR_POINT_SPRITE      = NAK_ATTR_POINT_SPRITE_S,
   NAK_ATTR_FOG_COORD         = 0x2e8,
   /* Reserved                  0x2ec */
   NAK_ATTR_TESS_COORD_X      = 0x2f0,
   NAK_ATTR_TESS_COORD_Y      = 0x2f4,
   NAK_ATTR_TESS_COORD        = NAK_ATTR_TESS_COORD_X,
   NAK_ATTR_INSTANCE_ID       = 0x2f8,
   NAK_ATTR_VERTEX_ID         = 0x2fc,

   NAK_ATTR_BARY_COORD_NO_PERSP_X = 0x3a8,
   NAK_ATTR_BARY_COORD_NO_PERSP_Y = 0x3ac,
   NAK_ATTR_BARY_COORD_NO_PERSP_Z = 0x3b0,
   NAK_ATTR_BARY_COORD_NO_PERSP = NAK_ATTR_BARY_COORD_NO_PERSP_X,

   NAK_ATTR_BARY_COORD_X = 0x3b4,
   NAK_ATTR_BARY_COORD_Y = 0x3b8,
   NAK_ATTR_BARY_COORD_Z = 0x3bc,
   NAK_ATTR_BARY_COORD = NAK_ATTR_BARY_COORD_X,

   /* Not in SPH */
   NAK_ATTR_FRONT_FACE        = 0x3fc,
};

static inline uint16_t
nak_attribute_attr_addr(UNUSED const struct nak_compiler *nak,
                        gl_vert_attrib attrib)
{
   assert(attrib >= VERT_ATTRIB_GENERIC0);
   return NAK_ATTR_GENERIC_START + (attrib - VERT_ATTRIB_GENERIC0) * 0x10;
}

uint16_t nak_varying_attr_addr(const struct nak_compiler *nak,
                               gl_varying_slot slot);
uint16_t nak_sysval_attr_addr(const struct nak_compiler *nak,
                              gl_system_value sysval);

enum ENUM_PACKED nak_sv {
   NAK_SV_LANE_ID          = 0x00,
   NAK_SV_VIRTCFG          = 0x02,
   NAK_SV_VIRTID           = 0x03,
   NAK_SV_PRIM_TYPE        = 0x10,
   NAK_SV_INVOCATION_ID    = 0x11,
   NAK_SV_THREAD_KILL      = 0x13,
   NAK_SV_INVOCATION_INFO  = 0x1d,
   NAK_SV_COMBINED_TID     = 0x20,
   NAK_SV_TID_X            = 0x21,
   NAK_SV_TID_Y            = 0x22,
   NAK_SV_TID_Z            = 0x23,
   NAK_SV_TID              = NAK_SV_TID_X,
   NAK_SV_CTAID_X          = 0x25,
   NAK_SV_CTAID_Y          = 0x26,
   NAK_SV_CTAID_Z          = 0x27,
   NAK_SV_CTAID            = NAK_SV_CTAID_X,
   NAK_SV_LANEMASK_EQ      = 0x38,
   NAK_SV_LANEMASK_LT      = 0x39,
   NAK_SV_LANEMASK_LE      = 0x3a,
   NAK_SV_LANEMASK_GT      = 0x3b,
   NAK_SV_LANEMASK_GE      = 0x3c,
   NAK_SV_CLOCK_LO         = 0x50,
   NAK_SV_CLOCK_HI         = 0x51,
   NAK_SV_CLOCK            = NAK_SV_CLOCK_LO,
   NAK_SV_VARIABLE_RATE    = 0x84,
};

bool nak_nir_workgroup_has_one_subgroup(const nir_shader *nir);

nir_def *nak_nir_load_sysval(nir_builder *b, enum nak_sv idx,
                             enum gl_access_qualifier access);

struct nak_xfb_info
nak_xfb_from_nir(const struct nak_compiler *nak,
                 const struct nir_xfb_info *nir_xfb);

struct nak_io_addr_offset {
   nir_scalar base;
   int32_t offset;
};

struct nak_io_addr_offset
nak_get_io_addr_offset(nir_def *addr, uint8_t imm_bits);

enum nak_nir_tex_ref_type {
   /** Indicates that this is a bindless texture */
   NAK_NIR_TEX_REF_TYPE_BINDLESS,

   /** Indicates that this is a bound texture
    *
    * The binding index provided in texture_index.
    */
   NAK_NIR_TEX_REF_TYPE_BOUND,

   /** Indicates that this is a cbuf texture
    *
    * texture_index is (idx << 16) | offset.
    */
   NAK_NIR_TEX_REF_TYPE_CBUF,
};

enum nak_nir_lod_mode {
   NAK_NIR_LOD_MODE_AUTO = 0,
   NAK_NIR_LOD_MODE_ZERO,
   NAK_NIR_LOD_MODE_BIAS,
   NAK_NIR_LOD_MODE_LOD,
   NAK_NIR_LOD_MODE_CLAMP,
   NAK_NIR_LOD_MODE_BIAS_CLAMP,
};

enum nak_nir_offset_mode {
   NAK_NIR_OFFSET_MODE_NONE = 0,
   NAK_NIR_OFFSET_MODE_AOFFI,
   NAK_NIR_OFFSET_MODE_PER_PX,
};

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nak_nir_tex_flags {
   enum nak_nir_tex_ref_type ref_type:2;
   enum nak_nir_lod_mode lod_mode:3;
   enum nak_nir_offset_mode offset_mode:2;
   bool has_z_cmpr:1;
   bool is_sparse:1;
   bool nodep:1;
   uint32_t pad:22;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nak_nir_tex_flags) == 4,
              "nak_nir_tex_flags has no holes");

#define NAK_AS_U32(x) ({\
   static_assert(sizeof(x) == 4, "x must be 4 bytes"); \
   uint32_t _u; \
   memcpy(&_u, &(x), 4); \
   _u; \
})

bool nak_nir_lower_scan_reduce(nir_shader *shader, const struct nak_compiler *nak);
bool nak_nir_lower_tex(nir_shader *nir, const struct nak_compiler *nak);
bool nak_nir_lower_gs_intrinsics(nir_shader *shader);
bool nak_nir_lower_algebraic_late(nir_shader *nir, const struct nak_compiler *nak);
bool nak_nir_lower_kepler_shared_atomics(nir_shader *shader);

struct nak_nir_attr_io_flags {
   bool output : 1;
   bool patch : 1;
   bool phys : 1;
   uint32_t pad:29;
};

enum nak_suclamp_mode {
   NAK_SUCLAMP_MODE_STORED_DESCRIPTOR,
   NAK_SUCLAMP_MODE_PITCH_LINEAR,
   NAK_SUCLAMP_MODE_BLOCK_LINEAR,
};

enum nak_suclamp_round {
   NAK_SUCLAMP_ROUND_R1,
   NAK_SUCLAMP_ROUND_R2,
   NAK_SUCLAMP_ROUND_R4,
   NAK_SUCLAMP_ROUND_R8,
   NAK_SUCLAMP_ROUND_R16,
};

struct nak_nir_suclamp_flags {
   enum nak_suclamp_mode mode : 2;
   enum nak_suclamp_round round : 3;
   bool is_s32 : 1;
   bool is_2d : 1;
   uint32_t pad:25;
};

enum nak_su_ga_offset_mode {
   NAK_SUGA_OFF_MODE_U32,
   NAK_SUGA_OFF_MODE_S32,
   NAK_SUGA_OFF_MODE_U8,
   NAK_SUGA_OFF_MODE_S8,
};

enum nak_imad_src_type {// 3 bits
   NAK_IMAD_TYPE_U32,
   NAK_IMAD_TYPE_U24,
   NAK_IMAD_TYPE_U16_LO,
   NAK_IMAD_TYPE_U16_HI,
   NAK_IMAD_TYPE_S32,
   NAK_IMAD_TYPE_S24,
   NAK_IMAD_TYPE_S16_LO,
   NAK_IMAD_TYPE_S16_HI,
};

struct nak_nir_imadsp_flags {
   enum nak_imad_src_type src0 : 3;
   enum nak_imad_src_type src1 : 3;
   enum nak_imad_src_type src2 : 3;
   bool params_from_src1 : 1;
   uint32_t pad:22;
};

bool nak_nir_lower_vtg_io(nir_shader *nir, const struct nak_compiler *nak);

enum nak_interp_mode {
   NAK_INTERP_MODE_PERSPECTIVE,
   NAK_INTERP_MODE_SCREEN_LINEAR,
   NAK_INTERP_MODE_CONSTANT,
};

enum nak_interp_freq {
    NAK_INTERP_FREQ_PASS,
    NAK_INTERP_FREQ_PASS_MUL_W,
    NAK_INTERP_FREQ_CONSTANT,
    NAK_INTERP_FREQ_STATE,
};

enum nak_interp_loc {
   NAK_INTERP_LOC_DEFAULT,
   NAK_INTERP_LOC_CENTROID,
   NAK_INTERP_LOC_OFFSET,
};

struct nak_nir_ipa_flags {
   enum nak_interp_mode interp_mode:2;
   enum nak_interp_freq interp_freq:2;
   enum nak_interp_loc interp_loc:2;
   uint32_t pad:26;
};

enum nak_cmat_type {
   NAK_CMAT_TYPE_M8N8K16_INT,
   NAK_CMAT_TYPE_M16N8K16_INT,
   NAK_CMAT_TYPE_M16N8K32_INT,

   NAK_CMAT_TYPE_M16N8K8_FLOAT,
   NAK_CMAT_TYPE_M16N8K16_FLOAT,

   /* Software emulated cmat layouts
    *
    * Those aren't supported as a single native *MMA invocation on any hardware,
    * so in order to support those we execute multiple *MMA instructions with a
    * register layout defined by us.
    */
   NAK_CMAT_TYPE_M16N16K32_INT_SW,
   NAK_CMAT_TYPE_M16N16K16_FLOAT_SW,
};

struct nak_nir_cmat_mul_add_flags {
   enum nak_cmat_type cmat_type:3;
   enum glsl_base_type a_type:5;
   enum glsl_base_type b_type:5;
   bool sat:1;
   uint32_t pad:18;
};

bool nak_nir_lower_fs_inputs(nir_shader *nir,
                             const struct nak_compiler *nak,
                             const struct nak_fs_key *fs_key);

enum nak_fs_out {
   NAK_FS_OUT_COLOR0 = 0x00,
   NAK_FS_OUT_COLOR1 = 0x10,
   NAK_FS_OUT_COLOR2 = 0x20,
   NAK_FS_OUT_COLOR3 = 0x30,
   NAK_FS_OUT_COLOR4 = 0x40,
   NAK_FS_OUT_COLOR5 = 0x50,
   NAK_FS_OUT_COLOR6 = 0x60,
   NAK_FS_OUT_COLOR7 = 0x70,
   NAK_FS_OUT_SAMPLE_MASK = 0x80,
   NAK_FS_OUT_DEPTH = 0x84,
};

#define NAK_FS_OUT_COLOR(n) (NAK_FS_OUT_COLOR0 + (n) * 16)

bool nak_nir_rematerialize_load_const(nir_shader *nir);
bool nak_nir_mark_lcssa_invariants(nir_shader *nir);
bool nak_nir_lower_non_uniform_ldcx(nir_shader *nir, const struct nak_compiler *nak);
bool nak_nir_add_barriers(nir_shader *nir, const struct nak_compiler *nak);
bool nak_nir_lower_cf(nir_shader *nir);
bool nak_nir_lower_cmat(nir_shader *shader, const struct nak_compiler *nak);

/**
 * Check if, for nak's purposes, a block is divergent
 *
 * Note that this differs from block->divergent because in nir's terms, the
 * start block of a loop with divergent continues can be non-divergent but nak
 * will always consider this divergent. This matters because nak does not allow
 * writing uregs from these blocks.
 */
bool nak_block_is_divergent(const nir_block *block);

void nak_optimize_nir(nir_shader *nir, const struct nak_compiler *nak);

#ifdef __cplusplus
}
#endif

#endif /* NAK_PRIVATE */
