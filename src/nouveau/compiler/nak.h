/*
 * Copyright © 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef NAK_H
#define NAK_H

#include "compiler/shader_enums.h"
#include "nir_defines.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAK_SUBGROUP_SIZE 32

struct nak_compiler;
struct nv_device_info;

struct nak_compiler *nak_compiler_create(const struct nv_device_info *dev);
void nak_compiler_destroy(struct nak_compiler *nak);

uint64_t nak_debug_flags(const struct nak_compiler *nak);

const struct nir_shader_compiler_options *
nak_nir_options(const struct nak_compiler *nak);

void nak_preprocess_nir(nir_shader *nir, const struct nak_compiler *nak);

bool nak_nir_lower_image_addrs(nir_shader *nir, const struct nak_compiler *nak);

struct nak_sample_location {
   uint8_t x_u4 : 4;
   uint8_t y_u4 : 4;
};
static_assert(sizeof(struct nak_sample_location) == 1,
              "This struct has no holes");

struct nak_sample_mask {
   uint16_t sample_mask;
};
static_assert(sizeof(struct nak_sample_mask) == 2,
              "This struct has no holes");

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nak_fs_key {
   bool zs_self_dep;

   /** True if sample shading is forced on via an API knob such as
    * VkPipelineMultisampleStateCreateInfo::minSampleShading
    */
   bool force_sample_shading;
   bool uses_underestimate;

   /**
    * The constant buffer index and offset at which the sample locations and
    * pass sample masks tables lives.
    */
   uint8_t sample_info_cb;

   /**
    * The offset into sample_info_cb at which the sample locations live.  The
    * sample locations table is an array of nak_sample_location where each
    * sample location is two 4-bit unorm values packed into an 8-bit value
    * with the bottom 4 bits for x and the top 4 bits for y.
    */
   uint32_t sample_locations_offset;

   /**
    * The offset into sample_info_cb at which the sample masks table lives.
    * The sample masks table is an array of nak_sample_mask where each entry
    * represents the set of samples covered by that pass corresponding to that
    * sample in a multi-pass fragment shader invocaiton.
    */
   uint32_t sample_masks_offset;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nak_fs_key) == 12, "This struct has no holes");


void nak_postprocess_nir(nir_shader *nir, const struct nak_compiler *nak,
                         nir_variable_mode robust2_modes,
                         const struct nak_fs_key *fs_key);

enum ENUM_PACKED nak_ts_domain {
   NAK_TS_DOMAIN_ISOLINE = 0,
   NAK_TS_DOMAIN_TRIANGLE = 1,
   NAK_TS_DOMAIN_QUAD = 2,
};

enum ENUM_PACKED nak_ts_spacing {
   NAK_TS_SPACING_INTEGER = 0,
   NAK_TS_SPACING_FRACT_ODD = 1,
   NAK_TS_SPACING_FRACT_EVEN = 2,
};

enum ENUM_PACKED nak_ts_prims {
   NAK_TS_PRIMS_POINTS = 0,
   NAK_TS_PRIMS_LINES = 1,
   NAK_TS_PRIMS_TRIANGLES_CW = 2,
   NAK_TS_PRIMS_TRIANGLES_CCW = 3,
};

struct nak_xfb_info {
   uint32_t stride[4];
   uint8_t stream[4];
   uint8_t attr_count[4];
   uint8_t attr_index[4][128];
};

/* This struct MUST have explicit padding fields to ensure that all padding is
 * zeroed and the zeros get properly copied, even across API boundaries.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wpadded"
struct nak_shader_info {
   mesa_shader_stage stage;

   /** Shader model */
   uint8_t sm;

   /** Number of GPRs used */
   uint8_t num_gprs;

   /**
    * Number of control barriers used
    *
    * These are barriers in the sense of glsl barrier(), not reconvergence
    * barriers. In CUDA, these barriers have an index, but we currently
    * only use index zero for vulkan, which means this will be at most 1.
    */
   uint8_t num_control_barriers;

   uint8_t _pad0;

   /** Maximum number of warps per SM based on static information */
   uint32_t max_warps_per_sm;

   /** Number of instructions used */
   uint32_t num_instrs;

   /** Number of cycles used by fixed-latency instructions */
   uint64_t num_static_cycles;

   /** Number of spills from GPRs to Memory */
   uint32_t num_spills_to_mem;

   /** Number of fills from Memory to GPRs */
   uint32_t num_fills_from_mem;

   /** Number of spills between register files */
   uint32_t num_spills_to_reg;

   /** Number of fills between register files */
   uint32_t num_fills_from_reg;

   /** Size of shader local (scratch) memory */
   uint32_t slm_size;

   /** Size of call/return stack in bytes/warp */
   uint32_t crs_size;

   union {
      struct {
         /* Local workgroup size */
         uint16_t local_size[3];

         /* Shared memory size */
         uint16_t smem_size;

         uint8_t _pad[4];
      } cs;

      struct {
         bool writes_depth;
         bool reads_sample_mask;
         bool post_depth_coverage;
         bool uses_sample_shading;
         bool early_fragment_tests;

         uint8_t _pad[7];
      } fs;

      struct {
         enum nak_ts_domain domain;
         enum nak_ts_spacing spacing;
         enum nak_ts_prims prims;

         uint8_t _pad[9];
      } ts;

      /* Used to initialize the union for other stages */
      uint8_t _pad[12];
   };

   struct {
      bool writes_layer;
      bool writes_point_size;
      bool writes_vprs_table_index;
      uint8_t clip_enable;
      uint8_t cull_enable;

      uint8_t _pad[3];

      struct nak_xfb_info xfb;
   } vtg;

   uint8_t _pad1[4];

   /** Shader header for 3D stages */
   uint32_t hdr[32];
};
#pragma GCC diagnostic pop

struct nak_shader_bin {
   struct nak_shader_info info;

   uint32_t code_size;
   const void *code;

   const char *asm_str;
};

void nak_shader_bin_destroy(struct nak_shader_bin *bin);

struct nak_shader_bin *
nak_compile_shader(nir_shader *nir, bool dump_asm,
                   const struct nak_compiler *nak,
                   nir_variable_mode robust2_modes,
                   const struct nak_fs_key *fs_key);

struct nak_qmd_cbuf {
   uint32_t index;
   uint32_t size;
   uint64_t addr;
};

struct nak_qmd_info {
   uint64_t addr;

   uint32_t smem_size;

   uint32_t global_size[3];

   uint32_t num_cbufs;
   struct nak_qmd_cbuf cbufs[8];
};

#define NAK_QMD_ALIGN_B 256
#define NAK_MAX_QMD_SIZE_B 384
#define NAK_MAX_QMD_DWORDS (NAK_MAX_QMD_SIZE_B / 4)

uint32_t nak_qmd_size_B(const struct nv_device_info *dev);

void nak_fill_qmd(const struct nv_device_info *dev,
                  const struct nak_shader_info *info,
                  const struct nak_qmd_info *qmd_info,
                  void *qmd_out, size_t qmd_size);

struct nak_qmd_dispatch_size_layout {
   uint16_t x_start, x_end;
   uint16_t y_start, y_end;
   uint16_t z_start, z_end;
};

struct nak_qmd_dispatch_size_layout
nak_get_qmd_dispatch_size_layout(const struct nv_device_info *dev);

struct nak_qmd_cbuf_desc_layout {
   uint16_t addr_shift;
   uint16_t addr_lo_start, addr_lo_end;
   uint16_t addr_hi_start, addr_hi_end;
};

struct nak_qmd_cbuf_desc_layout
nak_get_qmd_cbuf_desc_layout(const struct nv_device_info *dev, uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* NAK_H */
