/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_SHADER_H
#define PANVK_SHADER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "compiler/pan_compiler.h"

#include "pan_desc.h"
#include "pan_earlyzs.h"

#include "panvk_cmd_push_constant.h"
#include "panvk_descriptor_set.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"

#include "vk_pipeline_layout.h"

#include "vk_shader.h"

extern const struct vk_device_shader_ops panvk_per_arch(device_shader_ops);

#define MAX_VS_ATTRIBS 16

#if PAN_ARCH < 9

/* We could theoretically use the MAX_PER_SET values here (except for UBOs
 * where we're really limited to 256 on the shader side), but on Bifrost we
 * have to copy some tables around, which comes at an extra memory/processing
 * cost, so let's pick something smaller. */
#define MAX_PER_STAGE_SAMPLED_IMAGES 256
#define MAX_PER_STAGE_SAMPLERS 128
#define MAX_PER_STAGE_UNIFORM_BUFFERS MAX_PER_SET_UNIFORM_BUFFERS
#define MAX_PER_STAGE_STORAGE_BUFFERS 64
#define MAX_PER_STAGE_STORAGE_IMAGES 32
#define MAX_PER_STAGE_INPUT_ATTACHMENTS MAX_PER_SET_INPUT_ATTACHMENTS

#else

#define MAX_PER_STAGE_SAMPLED_IMAGES MAX_PER_SET_SAMPLED_IMAGES
#define MAX_PER_STAGE_SAMPLERS MAX_PER_SET_SAMPLERS
#define MAX_PER_STAGE_UNIFORM_BUFFERS MAX_PER_SET_UNIFORM_BUFFERS
#define MAX_PER_STAGE_STORAGE_BUFFERS MAX_PER_SET_STORAGE_BUFFERS
#define MAX_PER_STAGE_STORAGE_IMAGES MAX_PER_SET_STORAGE_IMAGES
#define MAX_PER_STAGE_INPUT_ATTACHMENTS MAX_PER_SET_INPUT_ATTACHMENTS

#endif

#define MAX_PER_STAGE_RESOURCES (                                              \
   MAX_PER_STAGE_SAMPLED_IMAGES + MAX_PER_STAGE_SAMPLERS +                     \
   MAX_PER_STAGE_UNIFORM_BUFFERS + MAX_PER_STAGE_STORAGE_BUFFERS +             \
   MAX_PER_STAGE_STORAGE_IMAGES + MAX_PER_STAGE_INPUT_ATTACHMENTS)

struct nir_shader;
struct pan_blend_state;
struct panvk_device;

enum panvk_varying_buf_id {
   PANVK_VARY_BUF_GENERAL,
   PANVK_VARY_BUF_POSITION,
   PANVK_VARY_BUF_PSIZ,

   /* Keep last */
   PANVK_VARY_BUF_MAX,
};

#if PAN_ARCH < 9
enum panvk_desc_table_id {
   PANVK_DESC_TABLE_USER = 0,
   PANVK_DESC_TABLE_CS_DYN_SSBOS = MAX_SETS,
   PANVK_DESC_TABLE_COMPUTE_COUNT = PANVK_DESC_TABLE_CS_DYN_SSBOS + 1,
   PANVK_DESC_TABLE_VS_DYN_SSBOS = MAX_SETS,
   PANVK_DESC_TABLE_FS_DYN_SSBOS = MAX_SETS + 1,
   PANVK_DESC_TABLE_GFX_COUNT = PANVK_DESC_TABLE_FS_DYN_SSBOS + 1,
};
#endif

#define PANVK_COLOR_ATTACHMENT(x) (x)
#define PANVK_ZS_ATTACHMENT       255

struct panvk_input_attachment_info {
   uint32_t target;
   uint32_t conversion;
};

/* One attachment per color, one for depth, one for stencil, and the last one
 * for the attachment without an InputAttachmentIndex attribute. */
#define INPUT_ATTACHMENT_MAP_SIZE 11

#define FAU_WORD_SIZE sizeof(uint64_t)

#define aligned_u64 __attribute__((aligned(sizeof(uint64_t)))) uint64_t

/* System values which are common to both graphics and compute.  These are
 * always at the same offset in both graphics and compute allowing us to
 * compile the shader without knowing which queue it will be dispatched on.
 */
struct panvk_common_sysvals_inner {
   /* Address of sysval/push constant buffer used for indirect loads */
   aligned_u64 push_uniforms;

   /* Address of the printf buffer */
   aligned_u64 printf_buffer_address;
} __attribute__((aligned(FAU_WORD_SIZE)));

struct panvk_common_sysvals {
   uint32_t _pad[4];
   struct panvk_common_sysvals_inner common;
} __attribute__((aligned(FAU_WORD_SIZE)));

static_assert((offsetof(struct panvk_common_sysvals, common) %
               FAU_WORD_SIZE) == 0,
              "struct panvk_graphics_sysvals_inner must be 8-byte aligned");
static_assert((sizeof(struct panvk_common_sysvals_inner) %
               FAU_WORD_SIZE) == 0,
              "struct panvk_graphics_sysvals_inner must be 8-byte aligned");

#define SYSVALS_COMMON_START \
   (offsetof(struct panvk_common_sysvals, common) / FAU_WORD_SIZE)

#define SYSVALS_COMMON_COUNT \
   (sizeof(struct panvk_common_sysvals_inner) / FAU_WORD_SIZE)

#define SYSVALS_COMMON_END (SYSVALS_COMMON_START + SYSVALS_COMMON_COUNT)

struct panvk_graphics_sysvals {
   /* Blend constants MUST come first because their position cannot depend on
    * the FAU packing of the fragment shader.
    */
   struct {
      float constants[4];
   } blend;

   /* This must be at the same offset for both compute and graphics */
   struct panvk_common_sysvals_inner common;

   struct {
      struct {
         float x, y, z;
      } scale, offset;
   } viewport;

   struct {
#if PAN_ARCH < 9
      int32_t raw_vertex_offset;
#endif
      int32_t first_vertex;
      int32_t base_instance;
      uint32_t noperspective_varyings;
   } vs;

   struct panvk_input_attachment_info iam[INPUT_ATTACHMENT_MAP_SIZE];

#if PAN_ARCH < 9
   /* gl_Layer on Bifrost is a bit of hack. We have to issue one draw per
    * layer, and filter primitives at the VS level.
    */
   int32_t layer_id;

   struct {
      aligned_u64 sets[PANVK_DESC_TABLE_GFX_COUNT];
   } desc;
#endif
} __attribute__((aligned(FAU_WORD_SIZE)));

static_assert(offsetof(struct panvk_graphics_sysvals, blend) == 0,
              "panvk_graphics_sysvals::blend must be at the start");
static_assert(offsetof(struct panvk_graphics_sysvals, common) ==
                 offsetof(struct panvk_common_sysvals, common),
              "Common sysvals must be at the same offset everywhere");
static_assert((sizeof(struct panvk_graphics_sysvals) % FAU_WORD_SIZE) == 0,
              "struct panvk_graphics_sysvals must be 8-byte aligned");
#if PAN_ARCH < 9
static_assert((offsetof(struct panvk_graphics_sysvals, desc) % FAU_WORD_SIZE) ==
                 0,
              "panvk_graphics_sysvals::desc must be 8-byte aligned");
#endif

struct panvk_compute_sysvals {
   struct {
      uint32_t x, y, z;
   } base;

   uint32_t _pad;

   /* This must be at the same offset for both compute and graphics */
   struct panvk_common_sysvals_inner common;

   struct {
      uint32_t x, y, z;
   } num_work_groups;
   struct {
      uint32_t x, y, z;
   } local_group_size;

#if PAN_ARCH < 9
   struct {
      aligned_u64 sets[PANVK_DESC_TABLE_COMPUTE_COUNT];
   } desc;
#endif
} __attribute__((aligned(FAU_WORD_SIZE)));

static_assert(offsetof(struct panvk_compute_sysvals, common) ==
                 offsetof(struct panvk_common_sysvals, common),
              "Common sysvals must be at the same offset everywhere");
static_assert((sizeof(struct panvk_compute_sysvals) % FAU_WORD_SIZE) == 0,
              "struct panvk_compute_sysvals must be 8-byte aligned");
#if PAN_ARCH < 9
static_assert((offsetof(struct panvk_compute_sysvals, desc) % FAU_WORD_SIZE) ==
                 0,
              "panvk_compute_sysvals::desc must be 8-byte aligned");
#endif

/* This is not the final offset in the push constant buffer (AKA FAU), but
 * just a magic offset we use before packing push constants so we can easily
 * identify the type of push constant (driver sysvals vs user push constants).
 */
#define SYSVALS_PUSH_CONST_BASE MAX_PUSH_CONSTANTS_SIZE

#define common_sysval_size(__name)                                             \
   sizeof(((struct panvk_common_sysvals *)NULL)->common.__name)

#define graphics_sysval_size(__name)                                           \
   sizeof(((struct panvk_graphics_sysvals *)NULL)->__name)

#define compute_sysval_size(__name)                                            \
   sizeof(((struct panvk_compute_sysvals *)NULL)->__name)

#define sysval_size(__ptype, __name) __ptype##_sysval_size(__name)

#define common_sysval_offset(__name)                                           \
   offsetof(struct panvk_common_sysvals, common.__name)

#define graphics_sysval_offset(__name)                                         \
   offsetof(struct panvk_graphics_sysvals, __name)

#define compute_sysval_offset(__name)                                          \
   offsetof(struct panvk_compute_sysvals, __name)

#define sysval_offset(__ptype, __name) __ptype##_sysval_offset(__name)

#define sysval_entry_size(__ptype, __name)                                     \
   sizeof(((struct panvk_##__ptype##_sysvals *)NULL)->__name[0])

#define sysval_entry_offset(__ptype, __name, __idx)                            \
   (sysval_offset(__ptype, __name) +                                           \
    (sysval_entry_size(__ptype, __name) * __idx))

#define sysval_fau_start(__ptype, __name)                                      \
   (sysval_offset(__ptype, __name) / FAU_WORD_SIZE)

#define sysval_fau_end(__ptype, __name)                                        \
   ((sysval_offset(__ptype, __name) + sysval_size(__ptype, __name) - 1) /      \
    FAU_WORD_SIZE)

#define sysval_fau_entry_start(__ptype, __name, __idx)                         \
   (sysval_entry_offset(__ptype, __name, __idx) / FAU_WORD_SIZE)

#define sysval_fau_entry_end(__ptype, __name, __idx)                           \
   ((sysval_entry_offset(__ptype, __name, __idx + 1) - 1) / FAU_WORD_SIZE)

#define shader_remapped_fau_offset(__shader, __kind, __offset)                 \
   ((FAU_WORD_SIZE * BITSET_PREFIX_SUM((__shader)->fau.used_##__kind,          \
                                       (__offset) / FAU_WORD_SIZE)) +          \
    ((__offset) % FAU_WORD_SIZE))

#define shader_remapped_sysval_offset(__shader, __offset)                      \
   shader_remapped_fau_offset(__shader, sysvals, __offset)

#define shader_remapped_push_const_offset(__shader, __offset)                  \
   (((__shader)->fau.sysval_count * FAU_WORD_SIZE) +                     \
    shader_remapped_fau_offset(__shader, push_consts, __offset))

#define shader_use_sysval(__shader, __ptype, __name)                           \
   BITSET_SET_RANGE((__shader)->fau.used_sysvals,                              \
                    sysval_fau_start(__ptype, __name),                         \
                    sysval_fau_end(__ptype, __name))

#define shader_uses_sysval(__shader, __ptype, __name)                          \
   BITSET_TEST_RANGE((__shader)->fau.used_sysvals,                             \
                     sysval_fau_start(__ptype, __name),                        \
                     sysval_fau_end(__ptype, __name))

#define shader_uses_sysval_entry(__shader, __ptype, __name, __idx)             \
   BITSET_TEST_RANGE((__shader)->fau.used_sysvals,                             \
                     sysval_fau_entry_start(__ptype, __name, __idx),           \
                     sysval_fau_entry_end(__ptype, __name, __idx))

#define shader_use_sysval_range(__shader, __base, __range)                     \
   BITSET_SET_RANGE((__shader)->fau.used_sysvals, (__base) / FAU_WORD_SIZE,    \
                    ((__base) + (__range) - 1) / FAU_WORD_SIZE)

#define shader_use_push_const_range(__shader, __base, __range)                 \
   BITSET_SET_RANGE((__shader)->fau.used_push_consts,                          \
                    (__base) / FAU_WORD_SIZE,                                  \
                    ((__base) + (__range) - 1) / FAU_WORD_SIZE)

#define load_sysval(__b, __ptype, __bitsz, __name)                             \
   nir_load_push_constant(                                                     \
      __b, sysval_size(__ptype, __name) / ((__bitsz) / 8), __bitsz,            \
      nir_imm_int(__b, sysval_offset(__ptype, __name)),                        \
      .base = SYSVALS_PUSH_CONST_BASE)

#define load_sysval_entry(__b, __ptype, __bitsz, __name, __dyn_idx)            \
   nir_load_push_constant(                                                     \
      __b, sysval_entry_size(__ptype, __name) / ((__bitsz) / 8), __bitsz,      \
      nir_imul_imm(__b, __dyn_idx, sysval_entry_size(__ptype, __name)),        \
      .base = SYSVALS_PUSH_CONST_BASE + sysval_offset(__ptype, __name),        \
      .range = sysval_size(__ptype, __name))

#if PAN_ARCH < 9
enum panvk_bifrost_desc_table_type {
   PANVK_BIFROST_DESC_TABLE_INVALID = -1,

   /* UBO is encoded on 8 bytes */
   PANVK_BIFROST_DESC_TABLE_UBO = 0,

   /* Images are using a <3DAttributeBuffer,Attribute> pair, each
    * of them being stored in a separate table. */
   PANVK_BIFROST_DESC_TABLE_IMG,

   /* Texture and sampler are encoded on 32 bytes */
   PANVK_BIFROST_DESC_TABLE_TEXTURE,
   PANVK_BIFROST_DESC_TABLE_SAMPLER,

   PANVK_BIFROST_DESC_TABLE_COUNT,
};
#endif

#define COPY_DESC_HANDLE(table, idx)           ((table << 28) | (idx))
#define COPY_DESC_HANDLE_EXTRACT_INDEX(handle) ((handle) & BITFIELD_MASK(28))
#define COPY_DESC_HANDLE_EXTRACT_TABLE(handle) ((handle) >> 28)

#define MAX_COMPUTE_SYSVAL_FAUS                                                \
   (sizeof(struct panvk_compute_sysvals) / FAU_WORD_SIZE)
#define MAX_GFX_SYSVAL_FAUS                                                    \
   (sizeof(struct panvk_graphics_sysvals) / FAU_WORD_SIZE)
#define MAX_SYSVAL_FAUS     MAX2(MAX_COMPUTE_SYSVAL_FAUS, MAX_GFX_SYSVAL_FAUS)
#define MAX_PUSH_CONST_FAUS (MAX_PUSH_CONSTANTS_SIZE / FAU_WORD_SIZE)

struct panvk_shader_fau_info {
   BITSET_DECLARE(used_sysvals, MAX_SYSVAL_FAUS);
   BITSET_DECLARE(used_push_consts, MAX_PUSH_CONST_FAUS);
   uint32_t sysval_count;
   uint32_t total_count;
};

struct panvk_shader_variant {
   struct pan_shader_info info;

   union {
      struct {
         struct pan_compute_dim local_size;
      } cs;

      struct {
         struct pan_earlyzs_lut earlyzs_lut;
         uint32_t input_attachment_read;
      } fs;
   };

   struct {
      uint32_t used_set_mask;

#if PAN_ARCH < 9
      struct {
         uint32_t map[MAX_DYNAMIC_UNIFORM_BUFFERS];
         uint32_t count;
      } dyn_ubos;
      struct {
         uint32_t map[MAX_DYNAMIC_STORAGE_BUFFERS];
         uint32_t count;
      } dyn_ssbos;
      struct {
         struct panvk_priv_mem map;
         uint32_t count[PANVK_BIFROST_DESC_TABLE_COUNT];
      } others;
#else
      struct {
         uint32_t map[MAX_DYNAMIC_BUFFERS];
         uint32_t count;
      } dyn_bufs;
      uint32_t max_varying_loads;
#endif
   } desc_info;

   struct panvk_shader_fau_info fau;

   const void *bin_ptr;
   uint32_t bin_size;
   bool own_bin;

   struct panvk_priv_mem code_mem;

#if PAN_ARCH < 9
   struct panvk_priv_mem rsd;
#else
   union {
      struct panvk_priv_mem spd;
      struct {
#if PAN_ARCH < 12
         struct panvk_priv_mem pos_points;
         struct panvk_priv_mem pos_triangles;
         struct panvk_priv_mem var;
#else
         struct panvk_priv_mem all_points;
         struct panvk_priv_mem all_triangles;
#endif
      } spds;
   };
#endif

   const char *nir_str;
   const char *asm_str;
};

enum panvk_vs_variant {
   /* Hardware vertex shader, when next stage is fragment */
   PANVK_VS_VARIANT_HW,

   PANVK_VS_VARIANTS,
};

struct panvk_shader {
   struct vk_shader vk;

   struct panvk_shader_variant variants[];
};

static inline unsigned
panvk_shader_num_variants(mesa_shader_stage stage)
{
   if (stage == MESA_SHADER_VERTEX)
      return PANVK_VS_VARIANTS;

   return 1;
}

static const char *panvk_vs_shader_variant_name[] = {
   [PANVK_VS_VARIANT_HW] = NULL,
};

static const char *
panvk_shader_variant_name(const struct panvk_shader *shader,
                          struct panvk_shader_variant *variant)
{
   unsigned i = variant - shader->variants;
   assert(i < panvk_shader_num_variants(shader->vk.stage));

   if (shader->vk.stage == MESA_SHADER_VERTEX) {
      assert(i < ARRAY_SIZE(panvk_vs_shader_variant_name));
      return panvk_vs_shader_variant_name[i];
   }

   assert(panvk_shader_num_variants(shader->vk.stage) == 1);

   return NULL;
}

static const struct panvk_shader_variant *
panvk_shader_only_variant(const struct panvk_shader *shader)
{
   if (!shader)
      return NULL;

   assert(panvk_shader_num_variants(shader->vk.stage) == 1);
   return &shader->variants[0];
}

static const struct panvk_shader_variant *
panvk_shader_hw_variant(const struct panvk_shader *shader)
{
   if (!shader)
      return NULL;

   return &shader->variants[0];
}

static inline uint64_t
panvk_shader_variant_get_dev_addr(const struct panvk_shader_variant *shader)
{
   return shader != NULL ? panvk_priv_mem_dev_addr(shader->code_mem) : 0;
}

#define panvk_shader_foreach_variant(__shader, __var)                          \
   for (struct panvk_shader_variant *__var = (__shader)->variants;             \
        __var < (__shader)->variants +                                         \
                   panvk_shader_num_variants((__shader)->vk.stage);            \
        ++__var)

#if PAN_ARCH < 9
struct panvk_shader_link {
   struct {
      struct panvk_priv_mem attribs;
   } vs, fs;
   unsigned buf_strides[PANVK_VARY_BUF_MAX];
};

VkResult panvk_per_arch(link_shaders)(struct panvk_pool *desc_pool,
                                      const struct panvk_shader_variant *vs,
                                      const struct panvk_shader_variant *fs,
                                      struct panvk_shader_link *link);

static inline void
panvk_shader_link_cleanup(struct panvk_shader_link *link)
{
   panvk_pool_free_mem(&link->vs.attribs);
   panvk_pool_free_mem(&link->fs.attribs);
}
#endif

void panvk_per_arch(nir_lower_descriptors)(
   nir_shader *nir, struct panvk_device *dev,
   const struct vk_pipeline_robustness_state *rs, uint32_t set_layout_count,
   struct vk_descriptor_set_layout *const *set_layouts,
   const struct vk_graphics_pipeline_state *state,
   struct panvk_shader_variant *shader);

/* This a stripped-down version of panvk_shader for internal shaders that
 * are managed by vk_meta (blend and preload shaders). Those don't need the
 * complexity inherent to user provided shaders as they're not exposed. */
struct panvk_internal_shader {
   struct vk_shader vk;
   struct pan_shader_info info;
   struct panvk_priv_mem code_mem;

#if PAN_ARCH < 9
   struct panvk_priv_mem rsd;
#else
   struct panvk_priv_mem spd;
#endif
};

#if PAN_ARCH >= 9
static inline bool
panvk_use_ld_var_buf(const struct panvk_shader_variant *shader)
{
   /* LD_VAR_BUF[_IMM] takes an 8-bit offset, limiting its use to 16 or less
    * varyings, assuming highp vec4. */
   if (shader->desc_info.max_varying_loads <= 16)
      return true;
   return false;
}
#endif

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_internal_shader, vk.base, VkShaderEXT,
                               VK_OBJECT_TYPE_SHADER_EXT)

VkResult panvk_per_arch(create_internal_shader)(
   struct panvk_device *dev, nir_shader *nir,
   struct pan_compile_inputs *compiler_inputs,
   struct panvk_internal_shader **shader_out);

VkResult panvk_per_arch(create_shader_from_binary)(
   struct panvk_device *dev, const struct pan_shader_info *info,
   struct pan_compute_dim local_size, const void *bin_ptr, size_t bin_size,
   struct panvk_shader **shader_out);

#endif
