/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_sampler.h"
#include "brw_eu_defines.h"

#define DEFINE_COND(name, condition)                                             \
   static bool                                                                   \
   name(const nir_tex_instr *tex, const struct intel_device_info *devinfo)       \
   {                                                                             \
      return (condition);                                                        \
   }                                                                             \
                                                                                 \
   static bool                                                                   \
   not_##name(const nir_tex_instr *tex, const struct intel_device_info *devinfo) \
   {                                                                             \
      return !(condition);                                                       \
   }                                                                             \

DEFINE_COND(gfx200_cube_array,
            devinfo->verx10 >= 200 &&
            tex->is_array &&
            tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE);
DEFINE_COND(gfx200_non_cube_array,
            devinfo->verx10 >= 200 &&
            !(tex->is_array &&
              tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE));
DEFINE_COND(gfx125, devinfo->verx10 >= 125);
DEFINE_COND(gfx200, devinfo->verx10 >= 200);
DEFINE_COND(gfx200_2darray,
            devinfo->verx10 >= 200 &&
            tex->is_array &&
            tex->sampler_dim == GLSL_SAMPLER_DIM_2D);

/* This array defines the availability of a particular opcode for a given NIR
 * tex instructions and platform. An entry without a callback is assumed to be
 * available on all platforms.
 *
 * We might revisit at some point to have one of this table per generation to
 * tune down the crazy.
 */
typedef bool (*opcode_filter_cb)(const nir_tex_instr *, const struct intel_device_info *);

static const opcode_filter_cb opcode_filters[BRW_SAMPLER_OPCODE_MAX] = {
   [BRW_SAMPLER_OPCODE_SAMPLE_B]              = not_gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_B_REDUCED]      = gfx200_non_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_B_PACKED]       = gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_L]              = not_gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_L_REDUCED]      = gfx200_non_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_L_PACKED]       = gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_D]              = not_gfx125,
   [BRW_SAMPLER_OPCODE_SAMPLE_D_REDUCED]      = gfx125,
   [BRW_SAMPLER_OPCODE_SAMPLE_B_C]            = not_gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_B_C_PACKED]     = gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_L_C]            = not_gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_L_C_PACKED]     = gfx200_cube_array,
   [BRW_SAMPLER_OPCODE_SAMPLE_D_C]            = not_gfx200_2darray,
   [BRW_SAMPLER_OPCODE_SAMPLE_D_C_PACKED]     = gfx200_2darray,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO]             = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_B]           = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_C]           = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_C_L]         = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_C_LZ]        = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_D]           = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_L]           = gfx200,
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_LZ]          = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_B]             = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_I]             = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_I_C]           = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_L]             = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_L_C]           = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO]            = not_gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_PACKED]     = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_B]          = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_C]          = not_gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_C_PACKED]   = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_I]          = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_I_C]        = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_L]          = gfx200,
   [BRW_SAMPLER_OPCODE_GATHER4_PO_L_C]        = gfx200,
   [BRW_SAMPLER_OPCODE_LD2DMS_W]              = not_gfx125,
   [BRW_SAMPLER_OPCODE_LD2DMS_W_GFX125]       = gfx125,
};

#define N(name) BITFIELD_BIT(nir_tex_src_##name)
#define R(name) { BRW_SAMPLER_PAYLOAD_PARAM_##name, false }
#define O(name) { BRW_SAMPLER_PAYLOAD_PARAM_##name, true  }

/* This array defines all the possible sampler payload formats. Quite a few
 * entry end up being duplicated due to changes from generation to generation.
 */
static const struct sampler_opcode_desc {
   const char *name;
   uint32_t hw_opcode;
   uint32_t nir_src_mask;
   bool is_fetch:1;
   bool is_gather:1;
   bool lod_zero:1;
   bool has_offset_payload:1;
   bool is_gather_implicit_lod:1;
   struct brw_sampler_payload_desc payload;
} sampler_opcode_descs[] = {
   [BRW_SAMPLER_OPCODE_SAMPLE] = {
      .name = "sample",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE,
      .nir_src_mask = N(coord) | N(min_lod) | N(offset),
      .payload = {
         .sources = {
            R(U), R(V), O(R), O(AI), O(MLOD),
         },
      }
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_B] = {
      .name = "sample_b",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS,
      .nir_src_mask = N(coord) | N(bias) | N(min_lod) | N(offset),
      .payload = {
         .sources = {
            R(BIAS), R(U), O(V), O(R), O(AI), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_B_PACKED] = {
      .name = "sample_b (packed)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS,
      .nir_src_mask = N(coord) | N(bias) | N(min_lod) | N(offset),
      .payload = {
         .sources = {
            R(BIAS_AI), R(U), O(V), O(R), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_B_REDUCED] = {
      .name = "sample_b (reduced)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS,
      .nir_src_mask = N(coord) | N(bias) | N(min_lod) | N(offset),
      .payload = {
         .sources = {
            R(BIAS), R(U), O(V), O(R), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_L] = {
      .name = "sample_l",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_LOD,
      .nir_src_mask = N(coord) | N(lod) | N(offset),
      .payload = {
         .sources = {
            R(LOD), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_L_PACKED] = {
      .name = "sample_l (packed)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_LOD,
      .nir_src_mask = N(coord) | N(lod) | N(offset),
      .payload = {
         .sources = {
            R(LOD_AI), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_L_REDUCED] = {
      .name = "sample_l (reduced)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_LOD,
      .nir_src_mask = N(coord) | N(lod) | N(offset),
      .payload = {
         .sources = {
            R(LOD), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_C] = {
      .name = "sample_c",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_COMPARE,
      .nir_src_mask = N(comparator) | N(coord) | N(min_lod) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(U), O(V), O(R), O(AI), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_D] = {
      .name = "sample_d",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_DERIVS,
      .nir_src_mask = N(coord) | N(min_lod) | N(ddx) | N(ddy) | N(offset),
      .payload = {
         .sources = {
            R(U), R(DUDX), R(DUDY), O(V), O(DVDX), O(DVDY), O(R), O(DRDX), O(DRDY), O(AI), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_D_REDUCED] = {
      .name = "sample_d (reduced)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_DERIVS,
      .nir_src_mask = N(coord) | N(min_lod) | N(ddx) | N(ddy) | N(offset),
      .payload = {
         .sources = {
            R(U), R(DUDX), R(DUDY), O(V), O(DVDX), O(DVDY), O(R), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_B_C] = {
      .name = "sample_b_c",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE,
      .nir_src_mask = N(comparator) | N(bias) | N(coord) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(BIAS), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_B_C_PACKED] = {
      .name = "sample_b_c (packed)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE,
      .nir_src_mask = N(comparator) | N(bias) | N(coord) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(BIAS_AI), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_D_C] = {
      .name = "sample_d_c",
      .hw_opcode = HSW_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE,
      .nir_src_mask = N(comparator) | N(coord) | N(ddx) | N(ddy) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(U), R(DUDX), R(DUDY), O(V), O(DVDX), O(DVDY), O(R), O(DRDX), O(DRDY), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_D_C_PACKED] = {
      .name = "sample_d_c (packed)",
      .hw_opcode = HSW_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE,
      .nir_src_mask = N(comparator) | N(coord) | N(ddx) | N(ddy) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(U), R(DUDX), R(DUDY), O(V), O(DVDX), O(DVDY), O(MLOD_R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_L_C] = {
      .name = "sample_l_c",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE,
      .nir_src_mask = N(comparator) | N(lod) | N(coord) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(LOD), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_L_C_PACKED] = {
      .name = "sample_l_c (packed)",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE,
      .nir_src_mask = N(comparator) | N(lod) | N(coord) | N(offset),
      .payload = {
         .sources = {
            R(REF), R(LOD_AI), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_LZ] = {
      .name = "sample_lz",
      .hw_opcode = GFX9_SAMPLER_MESSAGE_SAMPLE_LZ,
      .nir_src_mask = N(coord) | N(lod) | N(offset),
      .lod_zero = true,
      .payload = {
         .sources = {
            R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_C_LZ] = {
      .name = "sample_c_lz",
      .hw_opcode = GFX9_SAMPLER_MESSAGE_SAMPLE_C_LZ,
      .nir_src_mask = N(comparator) | N(coord) | N(lod) | N(offset),
      .lod_zero = true,
      .payload = {
         .sources = {
            R(REF), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO] = {
      .name = "sample_po",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO,
      .nir_src_mask = N(coord) | N(offset) | N(min_lod),
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(U), R(V), R(R), R(OFFUVR4), O(MLOD)
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_B] = {
      .name = "sample_po_b",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_BIAS,
      .nir_src_mask = N(bias) | N(coord) | N(offset) | N(min_lod),
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(BIAS_OFFUVR4), R(U), O(V), O(R), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_C] = {
      .name = "sample_po_c",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_COMPARE,
      .nir_src_mask = N(comparator) | N(coord) | N(offset) | N(min_lod),
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(U), R(V), R(OFFUV4_R), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_C_LZ] = {
      .name = "sample_po_c_lz",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_C_LZ,
      .nir_src_mask = N(comparator) | N(lod) | N(coord) | N(offset),
      .has_offset_payload = true,
      .lod_zero = true,
      .payload = {
         .sources = {
            R(REF), R(U), R(V), R(OFFUV4_R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_C_L] = {
      .name = "sample_po_c_l",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD_COMPARE,
      .nir_src_mask = N(comparator) | N(lod) | N(coord) | N(offset),
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(LOD_OFFUVR4), R(U), O(V), O(R)
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_D] = {
      .name = "sample_po_d",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS,
      .nir_src_mask = N(ddx) | N(ddy) | N(coord) | N(offset) | N(min_lod),
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(U), R(DUDX), R(DUDY), R(V), R(DVDX), R(DVDY), R(OFFUVR4_R), O(MLOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_L] = {
      .name = "sample_po_l",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD,
      .nir_src_mask = N(lod) | N(coord) | N(offset),
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(LOD_OFFUVR4), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLE_PO_LZ] = {
      .name = "sample_po_lz",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_PO_LZ,
      .nir_src_mask = N(lod) | N(coord) | N(offset),
      .has_offset_payload = true,
      .lod_zero = true,
      .payload = {
         .sources = {
            R(U), R(V), R(R), R(OFFUVR4),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD] = {
      .name = "ld",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_LD,
      .nir_src_mask = N(lod) | N(coord) | N(offset),
      .is_fetch = true,
      .payload = {
         .sources = {
            R(U), O(V), R(LOD), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD_LZ] = {
      .name = "ld_lz",
      .hw_opcode = GFX9_SAMPLER_MESSAGE_SAMPLE_LD_LZ,
      .nir_src_mask = N(coord) | N(lod) | N(offset),
      .lod_zero = true,
      .is_fetch = true,
      .payload = {
         .sources = {
            R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LOD] = {
      .name = "lod",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_LOD,
      .nir_src_mask = N(coord),
      .payload = {
         .sources = {
            R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_RESINFO] = {
      .name = "resinfo",
      .hw_opcode = GFX5_SAMPLER_MESSAGE_SAMPLE_RESINFO,
      .nir_src_mask = N(lod),
      .payload = {
         .sources = {
            R(LOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_SAMPLEINFO] = {
      .name = "sampleinfo",
      .hw_opcode = GFX6_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO,
   },
   [BRW_SAMPLER_OPCODE_GATHER4] = {
      .name = "gather4",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4,
      .nir_src_mask = N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_B] = {
      .name = "gather4_b",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B,
      .nir_src_mask = N(bias) | N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(BIAS), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_C] = {
      .name = "gather4_c",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_C,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(REF), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_I] = {
      .name = "gather4_i",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I,
      .nir_src_mask = N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_I_C] = {
      .name = "gather4_i_c",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(REF), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_L] = {
      .name = "gather4_l",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L,
      .nir_src_mask = N(lod) | N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(LOD), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_L_C] = {
      .name = "gather4_l_c",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C,
      .nir_src_mask = N(comparator) | N(lod) | N(coord) | N(offset),
      .is_gather = true,
      .payload = {
         .sources = {
            R(REF), R(LOD), R(U), O(V), O(R), O(AI),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO] = {
      .name = "gather4_po",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO,
      .nir_src_mask = N(coord) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(U), O(V), R(OFFU), O(OFFV), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_PACKED] = {
      .name = "gather4_po (packed)",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO,
      .nir_src_mask = N(coord) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(U), O(V), R(OFFUV6), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_B] = {
      .name = "gather4_po_b",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_B,
      .nir_src_mask = N(bias) | N(coord) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(BIAS_OFFUV6), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_C] = {
      .name = "gather4_po_c",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(U), O(V), R(OFFU), O(OFFV), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_C_PACKED] = {
      .name = "gather4_po_c (packed)",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(U), O(V), R(OFFUVR6),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_I] = {
      .name = "gather4_i",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I,
      .nir_src_mask = N(coord) | N(offset),
      .is_gather = true,
      .is_gather_implicit_lod = true,
      .payload = {
         .sources = {
            R(U), O(V), O(R), O(AI)
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_I] = {
      .name = "gather4_po_i",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .is_gather_implicit_lod = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(U), R(V), R(R), R(OFFUV6),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_I_C] = {
      .name = "gather4_po_i_c",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I_C,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .is_gather_implicit_lod = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(U), R(V), R(OFFUV6_R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_L] = {
      .name = "gather4_po_l",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L,
      .nir_src_mask = N(coord) | N(lod) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(LOD_OFFUV6), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_GATHER4_PO_L_C] = {
      .name = "gather4_po_l_c",
      .hw_opcode = XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L_C,
      .nir_src_mask = N(comparator) | N(coord) | N(offset),
      .is_gather = true,
      .has_offset_payload = true,
      .payload = {
         .sources = {
            R(REF), R(LOD_OFFUV6), R(U), O(V), O(R),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD2DMS_W] = {
      .name = "ld2dms_w",
      .hw_opcode = GFX9_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W,
      .nir_src_mask = N(ms_index) | N(ms_mcs_intel) | N(coord) | N(lod) | N(offset),
      .is_fetch = true,
      .payload = {
         .sources = {
            R(SI), R(MCSL), R(MCSH), R(U), O(V), O(R), O(LOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD2DMS_W_GFX125] = {
      .name = "ld2dms_w (gfx125)",
      .hw_opcode = GFX9_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W,
      .nir_src_mask = N(ms_index) | N(ms_mcs_intel) | N(coord) | N(lod) | N(offset),
      .is_fetch = true,
      .payload = {
         .sources = {
            R(SI), R(MCS0), R(MCS1), R(MCS2), R(MCS3), R(U), O(V), O(R), O(LOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD_MCS] = {
      .name = "ld_mcs",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_LD_MCS,
      .nir_src_mask = 0 /* internal */,
      .is_fetch = true,
      .payload = {
         .sources = {
            R(U), O(V), O(R), O(LOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD2DMS] = {
      .name = "ld2dms",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_LD2DMS,
      .nir_src_mask = 0 /* internal */,
      .is_fetch = true,
      .payload = {
         .sources = {
            R(SI), R(MCS), R(U), O(V), O(R), O(LOD),
         },
      },
   },
   [BRW_SAMPLER_OPCODE_LD2DSS] = {
      .name = "ld2dss",
      .hw_opcode = GFX7_SAMPLER_MESSAGE_SAMPLE_LD2DSS,
      .nir_src_mask = 0 /* internal */,
      .is_fetch = true,
      .payload = {
         .sources = {
            R(SSI), R(U), O(V), O(R), O(LOD),
         },
      },
   },
};

#undef R
#undef O

#define P(name, str) BRW_SAMPLER_PAYLOAD_PARAM_##name: return str

const char *
brw_sampler_payload_param_name(enum brw_sampler_payload_param param)
{
   switch (param) {
   case P(U,            "u");
   case P(V,            "v");
   case P(R,            "r");
   case P(AI,           "ai");
   case P(BIAS,         "bias");
   case P(LOD,          "lod");
   case P(MLOD,         "mlod");
   case P(REF,          "ref");
   case P(DUDX,         "dudx");
   case P(DUDY,         "dudy");
   case P(DVDX,         "dvdx");
   case P(DVDY,         "dvdy");
   case P(DRDX,         "drdx");
   case P(DRDY,         "drdy");
   case P(OFFU,         "offu");
   case P(OFFV,         "offv");
   case P(OFFUV6,       "offuv6");
   case P(OFFUVR6,      "offuvr6");
   case P(BIAS_AI,      "bias_ai");
   case P(BIAS_OFFUV6,  "bias_offuv6");
   case P(BIAS_OFFUVR4, "bias_offuvr4");
   case P(LOD_AI,       "lod_ai");
   case P(LOD_OFFUV6,   "lod_offuv6");
   case P(LOD_OFFUVR4,  "lod_offuvr4");
   case P(OFFUV4_R,     "offuv4_r");
   case P(OFFUV6_R,     "offuv6_r");
   case P(SI,           "si");
   case P(SSI,          "ssi");
   case P(MCS,          "mcs");
   case P(MCSL,         "mcsl");
   case P(MCSH,         "mcsh");
   case P(MCS0,         "mcs0");
   case P(MCS1,         "mcs1");
   case P(MCS2,         "mcs2");
   case P(MCS3,         "mcs3");
   default: UNREACHABLE("invalid param");
   }
}

#undef P

uint32_t
brw_get_sampler_hw_opcode(enum brw_sampler_opcode opcode)
{
   assert(opcode < ARRAY_SIZE(sampler_opcode_descs));
   return sampler_opcode_descs[opcode].hw_opcode;
}

bool
brw_sampler_opcode_is_gather(enum brw_sampler_opcode opcode)
{
   assert(opcode < ARRAY_SIZE(sampler_opcode_descs));
   return sampler_opcode_descs[opcode].is_gather;
}

const char *
brw_sampler_opcode_name(enum brw_sampler_opcode opcode)
{
   assert(opcode < ARRAY_SIZE(sampler_opcode_descs));
   return sampler_opcode_descs[opcode].name;
}

const struct brw_sampler_payload_desc *
brw_get_sampler_payload_desc(enum brw_sampler_opcode opcode)
{
   assert(opcode < ARRAY_SIZE(sampler_opcode_descs));
   return &sampler_opcode_descs[opcode].payload;
}

static uint32_t
opcode_sources(const struct sampler_opcode_desc *opcode)
{
   uint32_t count = 0;
   while (opcode->payload.sources[count].param != BRW_SAMPLER_PAYLOAD_PARAM_INVALID)
      count++;
   return count;
}

bool
brw_nir_tex_offset_in_constant_range(const nir_tex_instr *tex,
                                     unsigned offset_index)
{
   assert(tex->src[offset_index].src_type == nir_tex_src_offset);

   if (!nir_src_is_const(tex->src[offset_index].src))
      return false;

   const unsigned num_components =
      nir_tex_instr_src_size(tex, offset_index);
   for (unsigned i = 0; i < num_components; i++) {
      int offset = nir_src_comp_as_int(tex->src[offset_index].src, i);
      if (offset < -8 || offset > 7)
         return false;
   }

   return true;
}

enum brw_sampler_opcode
brw_get_sampler_opcode_from_tex(const struct intel_device_info *devinfo,
                                const nir_tex_instr *tex)
{
   /* Deal with some corner cases first */
   switch (tex->op) {
   case nir_texop_lod:              return BRW_SAMPLER_OPCODE_LOD;
   case nir_texop_query_levels:     return BRW_SAMPLER_OPCODE_RESINFO;
   case nir_texop_texture_samples:  return BRW_SAMPLER_OPCODE_SAMPLEINFO;
   case nir_texop_txf_ms_mcs_intel: return BRW_SAMPLER_OPCODE_LD_MCS;
   case nir_texop_txs:              return BRW_SAMPLER_OPCODE_RESINFO;
   default:                         break;
   }

   const bool is_fetch =
      tex->op == nir_texop_txf ||
      tex->op == nir_texop_txf_ms ||
      tex->op == nir_texop_txf_ms_fb ||
      tex->op == nir_texop_txf_ms_mcs_intel;

   const bool is_gather = tex->op == nir_texop_tg4;

   const int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   const bool lod_zero =
      lod_index >= 0 &&
      nir_src_is_const(tex->src[lod_index].src) &&
      nir_src_as_const_value(tex->src[lod_index].src)->u32 == 0;

   /* We can stuff the offsets into the message header if they are in the
    * encoding range [-8, 7]. Otherwise we need a payload slot for them.
    */
   bool offset_non_constant_or_non_header_range = false;
   const int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_index >= 0) {
      offset_non_constant_or_non_header_range =
         !brw_nir_tex_offset_in_constant_range(tex, offset_index);
   }

   uint32_t src_mask = 0;
   for (uint32_t i = 0; i < tex->num_srcs; i++)
      src_mask |= BITFIELD_BIT(tex->src[i].src_type);

   const uint32_t src_mask_ignore =
      N(texture_deref) |
      N(sampler_deref) |
      N(texture_offset) |
      N(sampler_offset) |
      N(texture_handle) |
      N(sampler_handle);

   src_mask &= ~src_mask_ignore;

#if DEBUG_SAMPLER_SELECTION
   fprintf(stderr, "NIR: ");
   nir_print_instr(&tex->instr, stderr);
   fprintf(stderr, "\n");
#define SKIP_IF(name, cond) {                                           \
      if (cond) {                                                       \
         fprintf(stderr, "%s: %s failed\n",                             \
                 brw_sampler_opcode_name(i), name);                     \
         continue;                                                      \
      }                                                                 \
   }
#else
#define SKIP_IF(name, cond) { if (cond) { continue; } }
#endif

   /* The sampler payloads described in this file are contiguous sets of
    * vector registers in the register file (Xe3+ can avoiding making this
    * contiguous) handed over to the sampler as input for a texture operation.
    * The format of the payloads are described above in sampler_opcode_descs[]
    * for each of the sampler opcode. Each payload element lives in a vector
    * register (or pair of vector register if the message is SIMD16/SIMD32,
    * depending on pre/post Xe2). And each lane of the shader subgroup
    * occupies a slot in each of the vector registers.
    *
    * Preceding the payload we can optionally add a header (a single vector
    * register) which does not hold per lane data, but instead data that is
    * common to all the lanes. This includes the sampler handle to use,
    * potential texture offsets (again the same for all the lanes), component
    * masking, sparse residency request, etc...
    *
    * Some opcodes allow for a per lane offsets, others don't. When we can't
    * use a per lane offset, we have to nir_lower_non_uniform_access texture
    * offsets like we do for sampler/texture handles and iterate through each
    * lane with the offset put into the sampler message header.
    *
    * We also have to consider that register space usage of per lane offsets.
    * In SIMD8 that's a single GRF per component, but on SIMD16 this is 2 GRFs
    * per component. So when the offset is constant or uniform across all
    * lanes, we want to put it in the header, since that will be combined with
    * other fields, reducing register usage.
    *
    * On Xe2+ platforms we can always find a sampler opcode that will
    * accomodate non constant offsets (Xe2 gained enough HW support). With the
    * opcodes ordered with per lane offsets at the bottom of the list we can
    * find the best matching opcode with one traversal.
    *
    * On pre-Xe2 platforms, we iterate through the opcodes twice, the first
    * iteration only considering the non constant offsets and the opcodes that
    * would accomodate them. The second iteration considering all the opcodes,
    * assuming the texture instructions were properly lowered with
    * nir_lower_non_uniform_access.
    */
   const uint32_t n_iterations = devinfo->ver < 20 ? 2 : 1;
   for (uint32_t iteration = 0; iteration < n_iterations; iteration++) {
      for (uint32_t i = 0; i < ARRAY_SIZE(sampler_opcode_descs); i++) {
         SKIP_IF("generation requirement not met",
                 opcode_filters[i] != NULL && !opcode_filters[i](tex, devinfo));

         SKIP_IF("non constant offsets",
                 iteration == 0 &&
                 offset_non_constant_or_non_header_range &&
                 !sampler_opcode_descs[i].has_offset_payload);

         SKIP_IF("not fetch instruction",
                 is_fetch != sampler_opcode_descs[i].is_fetch);

         SKIP_IF("not gather instruction",
                 is_gather != sampler_opcode_descs[i].is_gather);

         SKIP_IF("not gather implicit lod",
                 tex->is_gather_implicit_lod !=
                 sampler_opcode_descs[i].is_gather_implicit_lod);

         SKIP_IF("non lod zero",
                 !lod_zero && sampler_opcode_descs[i].lod_zero);

         SKIP_IF("non matching sources",
                 (sampler_opcode_descs[i].nir_src_mask & src_mask) != src_mask);

#if DEBUG_SAMPLER_SELECTION
         fprintf(stderr, "selected %s\n", brw_sampler_opcode_name(i));
#endif
         return (enum brw_sampler_opcode) i;
      }
   }

   UNREACHABLE("Cannot match tex instruction to HW opcode");
}
