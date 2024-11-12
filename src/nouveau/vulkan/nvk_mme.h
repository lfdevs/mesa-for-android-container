/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_MME_H
#define NVK_MME_H 1

#include "mme_builder.h"
#include "nvk_private.h"

#include "nak.h"

struct nv_device_info;

enum nvk_mme {
   NVK_MME_SELECT_CB0,
   NVK_MME_BIND_CBUF_DESC,
   NVK_MME_CLEAR,
   NVK_MME_BIND_IB,
   NVK_MME_BIND_VB,
   NVK_MME_SET_VB_ENABLES,
   NVK_MME_SET_VB_STRIDE,
   NVK_MME_SET_TESS_PARAMS,
   NVK_MME_SET_SHADING_RATE_CONTROL,
   NVK_MME_SET_ANTI_ALIAS,
   NVK_MME_DRAW,
   NVK_MME_DRAW_INDEXED,
   NVK_MME_DRAW_INDIRECT,
   NVK_MME_DRAW_INDEXED_INDIRECT,
   NVK_MME_DRAW_INDIRECT_COUNT,
   NVK_MME_DRAW_INDEXED_INDIRECT_COUNT,
   NVK_MME_ADD_CS_INVOCATIONS,
   NVK_MME_DISPATCH_INDIRECT,
   NVK_MME_WRITE_CS_INVOCATIONS,
   NVK_MME_XFB_COUNTER_LOAD,
   NVK_MME_XFB_DRAW_INDIRECT,
   NVK_MME_SET_PRIV_REG,
   NVK_MME_SET_WRITE_MASK,
   NVK_MME_SET_CONSERVATIVE_RASTER_STATE,
   NVK_MME_SET_VIEWPORT_MIN_MAX_Z,
   NVK_MME_SET_Z_CLAMP,

   NVK_MME_COUNT,
};

enum nvk_mme_scratch {
   /* These are reserved for communicating with FALCON */
   NVK_MME_SCRATCH_FALCON_0 = 0,
   NVK_MME_SCRATCH_FALCON_1 = 0,
   NVK_MME_SCRATCH_FALCON_2 = 0,

   NVK_MME_SCRATCH_CS_INVOCATIONS_HI,
   NVK_MME_SCRATCH_CS_INVOCATIONS_LO,
   NVK_MME_SCRATCH_DRAW_BEGIN,
   NVK_MME_SCRATCH_DRAW_COUNT,
   NVK_MME_SCRATCH_DRAW_PAD_DW,
   NVK_MME_SCRATCH_DRAW_IDX,
   NVK_MME_SCRATCH_VIEW_MASK,
   NVK_MME_SCRATCH_WRITE_MASK_DYN,
   NVK_MME_SCRATCH_WRITE_MASK_PIPELINE,
   NVK_MME_SCRATCH_CONSERVATIVE_RASTER_STATE,

   /* Bitfield of enabled vertex buffer bindings */
   NVK_MME_SCRATCH_VB_ENABLES,

   /* Tessellation parameters */
   NVK_MME_SCRATCH_TESS_PARAMS,

   /* Anti-aliasing state */
   NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_0,
   NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_1,
   NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_2,
   NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_3,
   NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_0,
   NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_1,
   NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_2,
   NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_3,
   NVK_MME_SCRATCH_ANTI_ALIAS,

   /* Shading rate control */
   NVK_MME_SCRATCH_SHADING_RATE_CONTROL,

   /* Addres of cb0 */
   NVK_MME_SCRATCH_CB0_ADDR_HI,
   NVK_MME_SCRATCH_CB0_ADDR_LO,

   /* Addres of zero page */
   NVK_MME_SCRATCH_ZERO_ADDR_HI,
   NVK_MME_SCRATCH_ZERO_ADDR_LO,

   /* Shadow copies of values in CB0 */
   NVK_MME_SCRATCH_CB0_FIRST_VERTEX,
   NVK_MME_SCRATCH_CB0_DRAW_INDEX,
   NVK_MME_SCRATCH_CB0_VIEW_INDEX,

   NVK_MME_SCRATCH_VIEWPORT0_MIN_Z,
   NVK_MME_SCRATCH_VIEWPORT0_MAX_Z,
   NVK_MME_SCRATCH_Z_CLAMP = NVK_MME_SCRATCH_VIEWPORT0_MIN_Z
                             + (NVK_MAX_VIEWPORTS * 2),

   /* Must be at the end */
   NVK_MME_NUM_SCRATCH,
};

#define NVK_SET_MME_SCRATCH(S) (0x3400 + (NVK_MME_SCRATCH_##S) * 4)

static inline void
_nvk_mme_load_scratch_to(struct mme_builder *b, struct mme_value val,
                         enum nvk_mme_scratch scratch)
{
   mme_state_to(b, val, 0x3400 + scratch * 4);
}
#define nvk_mme_load_scratch_to(b, v, S) \
   _nvk_mme_load_scratch_to(b, v, NVK_MME_SCRATCH_##S)

static inline struct mme_value
_nvk_mme_load_scratch(struct mme_builder *b, enum nvk_mme_scratch scratch)
{
   struct mme_value val = mme_alloc_reg(b);
   _nvk_mme_load_scratch_to(b, val, scratch);
   return val;
}
#define nvk_mme_load_scratch(b, S) \
   _nvk_mme_load_scratch(b, NVK_MME_SCRATCH_##S)

#define nvk_mme_load_scratch_arr(b, S, i) \
   _nvk_mme_load_scratch(b, NVK_MME_SCRATCH_##S + i)

static inline void
_nvk_mme_store_scratch(struct mme_builder *b, enum nvk_mme_scratch scratch,
                       struct mme_value data)
{
   mme_mthd(b, 0x3400 + scratch * 4);
   mme_emit(b, data);
}
#define nvk_mme_store_scratch(b, S, v) \
   _nvk_mme_store_scratch(b, NVK_MME_SCRATCH_##S, v)

static inline void
_nvk_mme_load_to_scratch(struct mme_builder *b, enum nvk_mme_scratch scratch)
{
   struct mme_value val = mme_load(b);
   _nvk_mme_store_scratch(b, scratch, val);
   mme_free_reg(b, val);
}
#define nvk_mme_load_to_scratch(b, S) \
   _nvk_mme_load_to_scratch(b, NVK_MME_SCRATCH_##S)

static inline uint32_t
nvk_mme_val_mask(uint16_t val, uint16_t mask)
{
   /* If there are bits in val which aren't in mask, it's probably a
    * programming error on the CPU side.  nvk_mme_set_masked() will still
    * work in this case but it's worth an assert.
    */
   assert(!(val & ~mask));

   return ((uint32_t)val) | (((uint32_t)mask) << 16);
}

/* This is a common pattern in NVK.  The input val_mask is a value plus a mask
 * where the top 16 bits are mask and the bottom 16 bits are data.  src is
 * copied and the bits in the mask are replaced by the corresponding value
 * bits in val_mask.
 */
static inline struct mme_value
nvk_mme_set_masked(struct mme_builder *b, struct mme_value src,
                   struct mme_value val_mask)
{
   struct mme_value mask = mme_merge(b, mme_zero(), val_mask, 0, 16, 16);
   struct mme_value val = mme_and_not(b, src, mask);

   /* Re-use the mask reg for val_mask & mask */
   mme_and_to(b, mask, val_mask, mask);
   mme_or_to(b, val, val, mask);
   mme_free_reg(b, mask);

   return val;
}

static void
_nvk_mme_spill(struct mme_builder *b, enum nvk_mme_scratch scratch,
               struct mme_value val)
{
   if (val.type == MME_VALUE_TYPE_REG) {
      _nvk_mme_store_scratch(b, scratch, val);
      mme_free_reg(b, val);
   }
}
#define nvk_mme_spill(b, S, v) \
   _nvk_mme_spill(b, NVK_MME_SCRATCH_##S, v)

static void
_nvk_mme_unspill(struct mme_builder *b, enum nvk_mme_scratch scratch,
                 struct mme_value val)
{
   if (val.type == MME_VALUE_TYPE_REG) {
      mme_realloc_reg(b, val);
      _nvk_mme_load_scratch_to(b, val, scratch);
   }
}
#define nvk_mme_unspill(b, S, v) \
   _nvk_mme_unspill(b, NVK_MME_SCRATCH_##S, v)

typedef void (*nvk_mme_builder_func)(struct mme_builder *b);

uint32_t *nvk_build_mme(const struct nv_device_info *devinfo,
                        enum nvk_mme mme, size_t *size_out);

void nvk_mme_select_cb0(struct mme_builder *b);
void nvk_mme_bind_cbuf_desc(struct mme_builder *b);
void nvk_mme_clear(struct mme_builder *b);
void nvk_mme_bind_ib(struct mme_builder *b);
void nvk_mme_bind_vb(struct mme_builder *b);
void nvk_mme_set_vb_enables(struct mme_builder *b);
void nvk_mme_set_vb_stride(struct mme_builder *b);
void nvk_mme_set_tess_params(struct mme_builder *b);
void nvk_mme_set_shading_rate_control(struct mme_builder *b);
void nvk_mme_set_anti_alias(struct mme_builder *b);
void nvk_mme_draw(struct mme_builder *b);
void nvk_mme_draw_indexed(struct mme_builder *b);
void nvk_mme_draw_indirect(struct mme_builder *b);
void nvk_mme_draw_indexed_indirect(struct mme_builder *b);
void nvk_mme_draw_indirect_count(struct mme_builder *b);
void nvk_mme_draw_indexed_indirect_count(struct mme_builder *b);
void nvk_mme_add_cs_invocations(struct mme_builder *b);
void nvk_mme_dispatch_indirect(struct mme_builder *b);
void nvk_mme_write_cs_invocations(struct mme_builder *b);
void nvk_mme_xfb_counter_load(struct mme_builder *b);
void nvk_mme_xfb_draw_indirect(struct mme_builder *b);
void nvk_mme_set_priv_reg(struct mme_builder *b);
void nvk_mme_set_write_mask(struct mme_builder *b);
void nvk_mme_set_conservative_raster_state(struct mme_builder *b);
void nvk_mme_set_viewport_min_max_z(struct mme_builder *b);
void nvk_mme_set_z_clamp(struct mme_builder *b);

uint32_t nvk_mme_tess_params(enum nak_ts_domain domain,
                             enum nak_ts_spacing spacing,
                             enum nak_ts_prims prims);
uint32_t nvk_mme_anti_alias_min_sample_shading(float mss);
uint32_t nvk_mme_shading_rate_control_sample_shading(bool sample_shading);

struct nvk_mme_mthd_data {
   uint16_t mthd;
   uint32_t data;
};

#define NVK_MME_MTHD_DATA_END ((struct nvk_mme_mthd_data) { 0, 0 })

struct nvk_mme_test_case {
   const struct nvk_mme_mthd_data *init;
   const uint32_t *params;
   const struct nvk_mme_mthd_data *expected;
   void (*check)(const struct nv_device_info *devinfo,
                 const struct nvk_mme_test_case *test,
                 const struct nvk_mme_mthd_data *results);
};

extern const struct nvk_mme_test_case nvk_mme_clear_tests[];
extern const struct nvk_mme_test_case nvk_mme_bind_vb_tests[];
extern const struct nvk_mme_test_case nvk_mme_set_tess_params_tests[];
extern const struct nvk_mme_test_case nvk_mme_set_shading_rate_control_tests[];
extern const struct nvk_mme_test_case nvk_mme_set_anti_alias_tests[];

void nvk_test_all_mmes(const struct nv_device_info *devinfo);

#endif /* NVK_MME_H */
