/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "util/u_atomic.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "vk_pipeline.h"
#include "vk_util.h"

#include "util/debug.h"
#include "ac_binary.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "aco_interface.h"
#include "sid.h"
#include "vk_format.h"

struct radv_blend_state {
   uint32_t blend_enable_4bit;
   uint32_t need_src_alpha;

   uint32_t cb_target_mask;
   uint32_t cb_target_enabled_4bit;
   uint32_t sx_mrt_blend_opt[8];
   uint32_t cb_blend_control[8];

   uint32_t spi_shader_col_format;
   uint32_t col_format_is_int8;
   uint32_t col_format_is_int10;
   uint32_t col_format_is_float32;
   uint32_t cb_shader_mask;
   uint32_t db_alpha_to_mask;

   uint32_t commutative_4bit;

   bool mrt0_is_dual_src;
};

struct radv_depth_stencil_state {
   uint32_t db_render_control;
   uint32_t db_render_override;
   uint32_t db_render_override2;
};

struct radv_dsa_order_invariance {
   /* Whether the final result in Z/S buffers is guaranteed to be
    * invariant under changes to the order in which fragments arrive.
    */
   bool zs;

   /* Whether the set of fragments that pass the combined Z/S test is
    * guaranteed to be invariant under changes to the order in which
    * fragments arrive.
    */
   bool pass_set;
};

static bool
radv_is_raster_enabled(const struct radv_graphics_pipeline *pipeline,
                       const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   return !pCreateInfo->pRasterizationState->rasterizerDiscardEnable ||
          (pipeline->dynamic_states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE);
}

static bool
radv_is_static_vrs_enabled(const struct radv_graphics_pipeline *pipeline,
                           const struct radv_graphics_pipeline_info *info)
{
   return info->fsr.size.width != 1 || info->fsr.size.height != 1 ||
          info->fsr.combiner_ops[0] != VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR ||
          info->fsr.combiner_ops[1] != VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
}

static bool
radv_is_vrs_enabled(const struct radv_graphics_pipeline *pipeline,
                    const struct radv_graphics_pipeline_info *info)
{
   return radv_is_static_vrs_enabled(pipeline, info) ||
          (pipeline->dynamic_states & RADV_DYNAMIC_FRAGMENT_SHADING_RATE);
}

static bool
radv_pipeline_has_ds_attachments(const struct radv_rendering_info *ri_info)
{
   return ri_info->depth_att_format != VK_FORMAT_UNDEFINED ||
          ri_info->stencil_att_format != VK_FORMAT_UNDEFINED;
}

static bool
radv_pipeline_has_color_attachments(const struct radv_rendering_info *ri_info)
{
   for (uint32_t i = 0; i < ri_info->color_att_count; ++i) {
      if (ri_info->color_att_formats[i] != VK_FORMAT_UNDEFINED)
         return true;
   }

   return false;
}

static bool
radv_pipeline_has_ngg(const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *shader = pipeline->base.shaders[pipeline->last_vgt_api_stage];

   return shader->info.is_ngg;
}

bool
radv_pipeline_has_ngg_passthrough(const struct radv_graphics_pipeline *pipeline)
{
   assert(radv_pipeline_has_ngg(pipeline));

   struct radv_shader *shader = pipeline->base.shaders[pipeline->last_vgt_api_stage];

   return shader->info.is_ngg_passthrough;
}

bool
radv_pipeline_has_gs_copy_shader(const struct radv_pipeline *pipeline)
{
   return !!pipeline->gs_copy_shader;
}

static struct radv_pipeline_slab *
radv_pipeline_slab_create(struct radv_device *device, struct radv_pipeline *pipeline,
                          uint32_t code_size)
{
   struct radv_pipeline_slab *slab;

   slab = calloc(1, sizeof(*slab));
   if (!slab)
      return NULL;

   slab->ref_count = 1;

   slab->alloc = radv_alloc_shader_memory(device, code_size, pipeline);
   if (!slab->alloc) {
      free(slab);
      return NULL;
   }

   return slab;
}

void
radv_pipeline_slab_destroy(struct radv_device *device, struct radv_pipeline_slab *slab)
{
   if (!p_atomic_dec_zero(&slab->ref_count))
      return;

   radv_free_shader_memory(device, slab->alloc);
   free(slab);
}

void
radv_pipeline_destroy(struct radv_device *device, struct radv_pipeline *pipeline,
                      const VkAllocationCallbacks *allocator)
{
   if (pipeline->type == RADV_PIPELINE_COMPUTE) {
      struct radv_compute_pipeline *compute_pipeline = radv_pipeline_to_compute(pipeline);

      free(compute_pipeline->rt_group_handles);
      free(compute_pipeline->rt_stack_sizes);
   } else if (pipeline->type == RADV_PIPELINE_LIBRARY) {
      struct radv_library_pipeline *library_pipeline = radv_pipeline_to_library(pipeline);

      free(library_pipeline->groups);
      for (uint32_t i = 0; i < library_pipeline->stage_count; i++) {
         RADV_FROM_HANDLE(vk_shader_module, module, library_pipeline->stages[i].module);
         if (module) {
            vk_object_base_finish(&module->base);
            ralloc_free(module);
         }
      }
      free(library_pipeline->stages);
      free(library_pipeline->identifiers);
      free(library_pipeline->hashes);
   }

   if (pipeline->slab)
      radv_pipeline_slab_destroy(device, pipeline->slab);

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i)
      if (pipeline->shaders[i])
         radv_shader_destroy(device, pipeline->shaders[i]);

   if (pipeline->gs_copy_shader)
      radv_shader_destroy(device, pipeline->gs_copy_shader);

   if (pipeline->cs.buf)
      free(pipeline->cs.buf);

   vk_object_base_finish(&pipeline->base);
   vk_free2(&device->vk.alloc, allocator, pipeline);
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyPipeline(VkDevice _device, VkPipeline _pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   radv_pipeline_destroy(device, pipeline, pAllocator);
}

uint32_t
radv_get_hash_flags(const struct radv_device *device, bool stats)
{
   uint32_t hash_flags = 0;

   if (device->physical_device->use_ngg_culling)
      hash_flags |= RADV_HASH_SHADER_USE_NGG_CULLING;
   if (device->instance->perftest_flags & RADV_PERFTEST_EMULATE_RT)
      hash_flags |= RADV_HASH_SHADER_EMULATE_RT;
   if (device->physical_device->rt_wave_size == 64)
      hash_flags |= RADV_HASH_SHADER_RT_WAVE64;
   if (device->physical_device->cs_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_CS_WAVE32;
   if (device->physical_device->ps_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_PS_WAVE32;
   if (device->physical_device->ge_wave_size == 32)
      hash_flags |= RADV_HASH_SHADER_GE_WAVE32;
   if (device->physical_device->use_llvm)
      hash_flags |= RADV_HASH_SHADER_LLVM;
   if (stats)
      hash_flags |= RADV_HASH_SHADER_KEEP_STATISTICS;
   if (device->robust_buffer_access) /* forces per-attribute vertex descriptors */
      hash_flags |= RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS;
   if (device->robust_buffer_access2) /* affects load/store vectorizer */
      hash_flags |= RADV_HASH_SHADER_ROBUST_BUFFER_ACCESS2;
   if (device->instance->debug_flags & RADV_DEBUG_SPLIT_FMA)
      hash_flags |= RADV_HASH_SHADER_SPLIT_FMA;
   return hash_flags;
}

static void
radv_pipeline_init_scratch(const struct radv_device *device, struct radv_pipeline *pipeline)
{
   unsigned scratch_bytes_per_wave = 0;
   unsigned max_waves = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (pipeline->shaders[i] && pipeline->shaders[i]->config.scratch_bytes_per_wave) {
         unsigned max_stage_waves = device->scratch_waves;

         scratch_bytes_per_wave =
            MAX2(scratch_bytes_per_wave, pipeline->shaders[i]->config.scratch_bytes_per_wave);

         max_stage_waves =
            MIN2(max_stage_waves, 4 * device->physical_device->rad_info.num_cu *
                 radv_get_max_waves(device, pipeline->shaders[i], i));
         max_waves = MAX2(max_waves, max_stage_waves);
      }
   }

   pipeline->scratch_bytes_per_wave = scratch_bytes_per_wave;
   pipeline->max_waves = max_waves;
}

static uint32_t
si_translate_blend_function(VkBlendOp op)
{
   switch (op) {
   case VK_BLEND_OP_ADD:
      return V_028780_COMB_DST_PLUS_SRC;
   case VK_BLEND_OP_SUBTRACT:
      return V_028780_COMB_SRC_MINUS_DST;
   case VK_BLEND_OP_REVERSE_SUBTRACT:
      return V_028780_COMB_DST_MINUS_SRC;
   case VK_BLEND_OP_MIN:
      return V_028780_COMB_MIN_DST_SRC;
   case VK_BLEND_OP_MAX:
      return V_028780_COMB_MAX_DST_SRC;
   default:
      return 0;
   }
}

static uint32_t
si_translate_blend_factor(enum amd_gfx_level gfx_level, VkBlendFactor factor)
{
   switch (factor) {
   case VK_BLEND_FACTOR_ZERO:
      return V_028780_BLEND_ZERO;
   case VK_BLEND_FACTOR_ONE:
      return V_028780_BLEND_ONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return V_028780_BLEND_SRC_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return V_028780_BLEND_ONE_MINUS_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
      return V_028780_BLEND_DST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return V_028780_BLEND_ONE_MINUS_DST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA:
      return V_028780_BLEND_SRC_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
      return V_028780_BLEND_ONE_MINUS_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA:
      return V_028780_BLEND_DST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return V_028780_BLEND_ONE_MINUS_DST_ALPHA;
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_CONSTANT_COLOR_GFX11
                                : V_028780_BLEND_CONSTANT_COLOR_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX11
                                 : V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX6;
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_CONSTANT_ALPHA_GFX11
                                 : V_028780_BLEND_CONSTANT_ALPHA_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX11
                                 : V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX6;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return V_028780_BLEND_SRC_ALPHA_SATURATE;
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_SRC1_COLOR_GFX11 : V_028780_BLEND_SRC1_COLOR_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_INV_SRC1_COLOR_GFX11
                                 : V_028780_BLEND_INV_SRC1_COLOR_GFX6;
   case VK_BLEND_FACTOR_SRC1_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_SRC1_ALPHA_GFX11 : V_028780_BLEND_SRC1_ALPHA_GFX6;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_INV_SRC1_ALPHA_GFX11
                                 : V_028780_BLEND_INV_SRC1_ALPHA_GFX6;
   default:
      return 0;
   }
}

static uint32_t
si_translate_blend_opt_function(unsigned op)
{
   switch (op) {
   case V_028780_COMB_DST_PLUS_SRC:
      return V_028760_OPT_COMB_ADD;
   case V_028780_COMB_SRC_MINUS_DST:
      return V_028760_OPT_COMB_SUBTRACT;
   case V_028780_COMB_DST_MINUS_SRC:
      return V_028760_OPT_COMB_REVSUBTRACT;
   case V_028780_COMB_MIN_DST_SRC:
      return V_028760_OPT_COMB_MIN;
   case V_028780_COMB_MAX_DST_SRC:
      return V_028760_OPT_COMB_MAX;
   default:
      return V_028760_OPT_COMB_BLEND_DISABLED;
   }
}

static uint32_t
si_translate_blend_opt_factor(unsigned factor, bool is_alpha)
{
   switch (factor) {
   case V_028780_BLEND_ZERO:
      return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_ALL;
   case V_028780_BLEND_ONE:
      return V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE;
   case V_028780_BLEND_SRC_COLOR:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0
                      : V_028760_BLEND_OPT_PRESERVE_C1_IGNORE_C0;
   case V_028780_BLEND_ONE_MINUS_SRC_COLOR:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1
                      : V_028760_BLEND_OPT_PRESERVE_C0_IGNORE_C1;
   case V_028780_BLEND_SRC_ALPHA:
      return V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0;
   case V_028780_BLEND_ONE_MINUS_SRC_ALPHA:
      return V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1;
   case V_028780_BLEND_SRC_ALPHA_SATURATE:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE
                      : V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;
   default:
      return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
   }
}

/**
 * Get rid of DST in the blend factors by commuting the operands:
 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
 */
static void
si_blend_remove_dst(unsigned *func, unsigned *src_factor, unsigned *dst_factor,
                    unsigned expected_dst, unsigned replacement_src)
{
   if (*src_factor == expected_dst && *dst_factor == V_028780_BLEND_ZERO) {
      *src_factor = V_028780_BLEND_ZERO;
      *dst_factor = replacement_src;

      /* Commuting the operands requires reversing subtractions. */
      if (*func == V_028780_COMB_SRC_MINUS_DST)
         *func = V_028780_COMB_DST_MINUS_SRC;
      else if (*func == V_028780_COMB_DST_MINUS_SRC)
         *func = V_028780_COMB_SRC_MINUS_DST;
   }
}

static bool
si_blend_factor_uses_dst(unsigned factor)
{
   return factor == V_028780_BLEND_DST_COLOR ||
          factor == V_028780_BLEND_DST_ALPHA ||
          factor == V_028780_BLEND_SRC_ALPHA_SATURATE ||
          factor == V_028780_BLEND_ONE_MINUS_DST_ALPHA ||
          factor == V_028780_BLEND_ONE_MINUS_DST_COLOR;
}

static bool
is_dual_src(enum amd_gfx_level gfx_level, unsigned factor)
{
   if (gfx_level >= GFX11) {
      switch (factor) {
      case V_028780_BLEND_SRC1_COLOR_GFX11:
      case V_028780_BLEND_INV_SRC1_COLOR_GFX11:
      case V_028780_BLEND_SRC1_ALPHA_GFX11:
      case V_028780_BLEND_INV_SRC1_ALPHA_GFX11:
         return true;
      default:
         return false;
      }
   } else {
      switch (factor) {
      case V_028780_BLEND_SRC1_COLOR_GFX6:
      case V_028780_BLEND_INV_SRC1_COLOR_GFX6:
      case V_028780_BLEND_SRC1_ALPHA_GFX6:
      case V_028780_BLEND_INV_SRC1_ALPHA_GFX6:
         return true;
      default:
         return false;
      }
   }
}

static unsigned
radv_choose_spi_color_format(const struct radv_device *device, VkFormat vk_format,
                             bool blend_enable, bool blend_need_alpha)
{
   const struct util_format_description *desc = vk_format_description(vk_format);
   bool use_rbplus = device->physical_device->rad_info.rbplus_allowed;
   struct ac_spi_color_formats formats = {0};
   unsigned format, ntype, swap;

   format = radv_translate_colorformat(vk_format);
   ntype = radv_translate_color_numformat(vk_format, desc,
                                          vk_format_get_first_non_void_channel(vk_format));
   swap = radv_translate_colorswap(vk_format, false);

   ac_choose_spi_color_formats(format, swap, ntype, false, use_rbplus, &formats);

   if (blend_enable && blend_need_alpha)
      return formats.blend_alpha;
   else if (blend_need_alpha)
      return formats.alpha;
   else if (blend_enable)
      return formats.blend;
   else
      return formats.normal;
}

static bool
format_is_int8(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   int channel = vk_format_get_first_non_void_channel(format);

   return channel >= 0 && desc->channel[channel].pure_integer && desc->channel[channel].size == 8;
}

static bool
format_is_int10(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   if (desc->nr_channels != 4)
      return false;
   for (unsigned i = 0; i < 4; i++) {
      if (desc->channel[i].pure_integer && desc->channel[i].size == 10)
         return true;
   }
   return false;
}

static bool
format_is_float32(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   int channel = vk_format_get_first_non_void_channel(format);

   return channel >= 0 &&
          desc->channel[channel].type == UTIL_FORMAT_TYPE_FLOAT && desc->channel[channel].size == 32;
}

static void
radv_pipeline_compute_spi_color_formats(const struct radv_graphics_pipeline *pipeline,
                                        const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                        struct radv_blend_state *blend,
                                        const struct radv_graphics_pipeline_info *info)
{
   unsigned col_format = 0, is_int8 = 0, is_int10 = 0, is_float32 = 0;
   unsigned num_targets;

   for (unsigned i = 0; i < info->ri.color_att_count; ++i) {
      unsigned cf;
      VkFormat fmt = info->ri.color_att_formats[i];

      if (fmt == VK_FORMAT_UNDEFINED || !(blend->cb_target_mask & (0xfu << (i * 4)))) {
         cf = V_028714_SPI_SHADER_ZERO;
      } else {
         bool blend_enable = blend->blend_enable_4bit & (0xfu << (i * 4));

         cf = radv_choose_spi_color_format(pipeline->base.device, fmt, blend_enable,
                                           blend->need_src_alpha & (1 << i));

         if (format_is_int8(fmt))
            is_int8 |= 1 << i;
         if (format_is_int10(fmt))
            is_int10 |= 1 << i;
         if (format_is_float32(fmt))
            is_float32 |= 1 << i;
      }

      col_format |= cf << (4 * i);
   }

   if (!(col_format & 0xf) && blend->need_src_alpha & (1 << 0)) {
      /* When a subpass doesn't have any color attachments, write the
       * alpha channel of MRT0 when alpha coverage is enabled because
       * the depth attachment needs it.
       */
      col_format |= V_028714_SPI_SHADER_32_AR;
   }

   /* If the i-th target format is set, all previous target formats must
    * be non-zero to avoid hangs.
    */
   num_targets = (util_last_bit(col_format) + 3) / 4;
   for (unsigned i = 0; i < num_targets; i++) {
      if (!(col_format & (0xfu << (i * 4)))) {
         col_format |= V_028714_SPI_SHADER_32_R << (i * 4);
      }
   }

   /* The output for dual source blending should have the same format as
    * the first output.
    */
   if (blend->mrt0_is_dual_src) {
      assert(!(col_format >> 4));
      col_format |= (col_format & 0xf) << 4;
   }

   blend->cb_shader_mask = ac_get_cb_shader_mask(col_format);
   blend->spi_shader_col_format = col_format;
   blend->col_format_is_int8 = is_int8;
   blend->col_format_is_int10 = is_int10;
   blend->col_format_is_float32 = is_float32;
}

/*
 * Ordered so that for each i,
 * radv_format_meta_fs_key(radv_fs_key_format_exemplars[i]) == i.
 */
const VkFormat radv_fs_key_format_exemplars[NUM_META_FS_KEYS] = {
   VK_FORMAT_R32_SFLOAT,
   VK_FORMAT_R32G32_SFLOAT,
   VK_FORMAT_R8G8B8A8_UNORM,
   VK_FORMAT_R16G16B16A16_UNORM,
   VK_FORMAT_R16G16B16A16_SNORM,
   VK_FORMAT_R16G16B16A16_UINT,
   VK_FORMAT_R16G16B16A16_SINT,
   VK_FORMAT_R32G32B32A32_SFLOAT,
   VK_FORMAT_R8G8B8A8_UINT,
   VK_FORMAT_R8G8B8A8_SINT,
   VK_FORMAT_A2R10G10B10_UINT_PACK32,
   VK_FORMAT_A2R10G10B10_SINT_PACK32,
};

unsigned
radv_format_meta_fs_key(struct radv_device *device, VkFormat format)
{
   unsigned col_format = radv_choose_spi_color_format(device, format, false, false);
   assert(col_format != V_028714_SPI_SHADER_32_AR);

   bool is_int8 = format_is_int8(format);
   bool is_int10 = format_is_int10(format);

   if (col_format == V_028714_SPI_SHADER_UINT16_ABGR && is_int8)
      return 8;
   else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR && is_int8)
      return 9;
   else if (col_format == V_028714_SPI_SHADER_UINT16_ABGR && is_int10)
      return 10;
   else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR && is_int10)
      return 11;
   else {
      if (col_format >= V_028714_SPI_SHADER_32_AR)
         --col_format; /* Skip V_028714_SPI_SHADER_32_AR  since there is no such VkFormat */

      --col_format; /* Skip V_028714_SPI_SHADER_ZERO */
      return col_format;
   }
}

static void
radv_blend_check_commutativity(enum amd_gfx_level gfx_level, struct radv_blend_state *blend,
                               unsigned op, unsigned src, unsigned dst, unsigned chanmask)
{
   bool is_src_allowed = false;

   /* Src factor is allowed when it does not depend on Dst. */
   if (src == V_028780_BLEND_ZERO ||
       src == V_028780_BLEND_ONE ||
       src == V_028780_BLEND_SRC_COLOR ||
       src == V_028780_BLEND_SRC_ALPHA ||
       src == V_028780_BLEND_SRC_ALPHA_SATURATE ||
       src == V_028780_BLEND_ONE_MINUS_SRC_COLOR ||
       src == V_028780_BLEND_ONE_MINUS_SRC_ALPHA) {
      is_src_allowed = true;
   }

   if (gfx_level >= GFX11) {
      if (src == V_028780_BLEND_CONSTANT_COLOR_GFX11 ||
          src == V_028780_BLEND_CONSTANT_ALPHA_GFX11 ||
          src == V_028780_BLEND_SRC1_COLOR_GFX11 ||
          src == V_028780_BLEND_SRC1_ALPHA_GFX11 ||
          src == V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX11 ||
          src == V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX11 ||
          src == V_028780_BLEND_INV_SRC1_COLOR_GFX11 ||
          src == V_028780_BLEND_INV_SRC1_ALPHA_GFX11) {
         is_src_allowed = true;
      }
   } else {
      if (src == V_028780_BLEND_CONSTANT_COLOR_GFX6 ||
          src == V_028780_BLEND_CONSTANT_ALPHA_GFX6 ||
          src == V_028780_BLEND_SRC1_COLOR_GFX6 ||
          src == V_028780_BLEND_SRC1_ALPHA_GFX6 ||
          src == V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX6 ||
          src == V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX6 ||
          src == V_028780_BLEND_INV_SRC1_COLOR_GFX6 ||
          src == V_028780_BLEND_INV_SRC1_ALPHA_GFX6) {
         is_src_allowed = true;
      }
   }

   if (dst == V_028780_BLEND_ONE && is_src_allowed) {
      /* Addition is commutative, but floating point addition isn't
       * associative: subtle changes can be introduced via different
       * rounding. Be conservative, only enable for min and max.
       */
      if (op == V_028780_COMB_MAX_DST_SRC || op == V_028780_COMB_MIN_DST_SRC)
         blend->commutative_4bit |= chanmask;
   }
}

static struct radv_blend_state
radv_pipeline_init_blend_state(struct radv_graphics_pipeline *pipeline,
                               const VkGraphicsPipelineCreateInfo *pCreateInfo,
                               const struct radv_graphics_pipeline_info *info)
{
   const struct radv_device *device = pipeline->base.device;
   struct radv_blend_state blend = {0};
   unsigned cb_color_control = 0;
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   int i;

   if (info->cb.logic_op_enable)
      cb_color_control |= S_028808_ROP3(info->cb.logic_op);
   else
      cb_color_control |= S_028808_ROP3(V_028808_ROP3_COPY);

   if (device->instance->debug_flags & RADV_DEBUG_NO_ATOC_DITHERING)
   {
      blend.db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_OFFSET0(2) | S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
                               S_028B70_ALPHA_TO_MASK_OFFSET2(2) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                               S_028B70_OFFSET_ROUND(0);
   }
   else
   {
      blend.db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_OFFSET0(3) | S_028B70_ALPHA_TO_MASK_OFFSET1(1) |
                               S_028B70_ALPHA_TO_MASK_OFFSET2(0) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                               S_028B70_OFFSET_ROUND(1);
   }

   if (info->ms.alpha_to_coverage_enable) {
      blend.db_alpha_to_mask |= S_028B70_ALPHA_TO_MASK_ENABLE(1);
      blend.need_src_alpha |= 0x1;
   }

   blend.cb_target_mask = 0;
   for (i = 0; i < info->cb.att_count; i++) {
      unsigned blend_cntl = 0;
      unsigned srcRGB_opt, dstRGB_opt, srcA_opt, dstA_opt;
      unsigned eqRGB = info->cb.att[i].color_blend_op;
      unsigned srcRGB = info->cb.att[i].src_color_blend_factor;
      unsigned dstRGB = info->cb.att[i].dst_color_blend_factor;
      unsigned eqA = info->cb.att[i].alpha_blend_op;
      unsigned srcA = info->cb.att[i].src_alpha_blend_factor;
      unsigned dstA = info->cb.att[i].dst_alpha_blend_factor;

      blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) |
                                  S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);

      if (!info->cb.att[i].color_write_mask)
         continue;

      /* Ignore other blend targets if dual-source blending
       * is enabled to prevent wrong behaviour.
       */
      if (blend.mrt0_is_dual_src)
         continue;

      blend.cb_target_mask |= (unsigned)info->cb.att[i].color_write_mask << (4 * i);
      blend.cb_target_enabled_4bit |= 0xfu << (4 * i);
      if (!info->cb.att[i].blend_enable) {
         blend.cb_blend_control[i] = blend_cntl;
         continue;
      }

      if (is_dual_src(gfx_level, srcRGB) || is_dual_src(gfx_level, dstRGB) ||
          is_dual_src(gfx_level, srcA) || is_dual_src(gfx_level, dstA))
         if (i == 0)
            blend.mrt0_is_dual_src = true;


      if (eqRGB == V_028780_COMB_MIN_DST_SRC || eqRGB == V_028780_COMB_MAX_DST_SRC) {
         srcRGB = V_028780_BLEND_ONE;
         dstRGB = V_028780_BLEND_ONE;
      }
      if (eqA == V_028780_COMB_MIN_DST_SRC || eqA == V_028780_COMB_MAX_DST_SRC) {
         srcA = V_028780_BLEND_ONE;
         dstA = V_028780_BLEND_ONE;
      }

      radv_blend_check_commutativity(gfx_level, &blend, eqRGB, srcRGB, dstRGB, 0x7u << (4 * i));
      radv_blend_check_commutativity(gfx_level, &blend, eqA, srcA, dstA, 0x8u << (4 * i));

      /* Blending optimizations for RB+.
       * These transformations don't change the behavior.
       *
       * First, get rid of DST in the blend factors:
       *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
       */
      si_blend_remove_dst(&eqRGB, &srcRGB, &dstRGB, V_028780_BLEND_DST_COLOR,
                          V_028780_BLEND_SRC_COLOR);

      si_blend_remove_dst(&eqA, &srcA, &dstA, V_028780_BLEND_DST_COLOR,
                          V_028780_BLEND_SRC_COLOR);

      si_blend_remove_dst(&eqA, &srcA, &dstA, V_028780_BLEND_DST_ALPHA,
                          V_028780_BLEND_SRC_ALPHA);

      /* Look up the ideal settings from tables. */
      srcRGB_opt = si_translate_blend_opt_factor(srcRGB, false);
      dstRGB_opt = si_translate_blend_opt_factor(dstRGB, false);
      srcA_opt = si_translate_blend_opt_factor(srcA, true);
      dstA_opt = si_translate_blend_opt_factor(dstA, true);

      /* Handle interdependencies. */
      if (si_blend_factor_uses_dst(srcRGB))
         dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
      if (si_blend_factor_uses_dst(srcA))
         dstA_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;

      if (srcRGB == V_028780_BLEND_SRC_ALPHA_SATURATE &&
          (dstRGB == V_028780_BLEND_ZERO || dstRGB == V_028780_BLEND_SRC_ALPHA ||
           dstRGB == V_028780_BLEND_SRC_ALPHA_SATURATE))
         dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;

      /* Set the final value. */
      blend.sx_mrt_blend_opt[i] =
         S_028760_COLOR_SRC_OPT(srcRGB_opt) | S_028760_COLOR_DST_OPT(dstRGB_opt) |
         S_028760_COLOR_COMB_FCN(si_translate_blend_opt_function(eqRGB)) |
         S_028760_ALPHA_SRC_OPT(srcA_opt) | S_028760_ALPHA_DST_OPT(dstA_opt) |
         S_028760_ALPHA_COMB_FCN(si_translate_blend_opt_function(eqA));
      blend_cntl |= S_028780_ENABLE(1);

      blend_cntl |= S_028780_COLOR_COMB_FCN(eqRGB);
      blend_cntl |= S_028780_COLOR_SRCBLEND(srcRGB);
      blend_cntl |= S_028780_COLOR_DESTBLEND(dstRGB);
      if (srcA != srcRGB || dstA != dstRGB || eqA != eqRGB) {
         blend_cntl |= S_028780_SEPARATE_ALPHA_BLEND(1);
         blend_cntl |= S_028780_ALPHA_COMB_FCN(eqA);
         blend_cntl |= S_028780_ALPHA_SRCBLEND(srcA);
         blend_cntl |= S_028780_ALPHA_DESTBLEND(dstA);
      }
      blend.cb_blend_control[i] = blend_cntl;

      blend.blend_enable_4bit |= 0xfu << (i * 4);

      if (srcRGB == V_028780_BLEND_SRC_ALPHA || dstRGB == V_028780_BLEND_SRC_ALPHA ||
          srcRGB == V_028780_BLEND_SRC_ALPHA_SATURATE ||
          dstRGB == V_028780_BLEND_SRC_ALPHA_SATURATE ||
          srcRGB == V_028780_BLEND_ONE_MINUS_SRC_ALPHA ||
          dstRGB == V_028780_BLEND_ONE_MINUS_SRC_ALPHA)
         blend.need_src_alpha |= 1 << i;
   }
   for (i = info->cb.att_count; i < 8; i++) {
      blend.cb_blend_control[i] = 0;
      blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) |
                                  S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);
   }

   if (device->physical_device->rad_info.has_rbplus) {
      /* Disable RB+ blend optimizations for dual source blending. */
      if (blend.mrt0_is_dual_src) {
         for (i = 0; i < 8; i++) {
            blend.sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_NONE) |
                                        S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_NONE);
         }
      }

      /* RB+ doesn't work with dual source blending, logic op and
       * RESOLVE.
       */
      if (blend.mrt0_is_dual_src || info->cb.logic_op_enable ||
          (device->physical_device->rad_info.gfx_level >= GFX11 && blend.blend_enable_4bit))
         cb_color_control |= S_028808_DISABLE_DUAL_QUAD(1);
   }

   if (blend.cb_target_mask)
      cb_color_control |= S_028808_MODE(V_028808_CB_NORMAL);
   else
      cb_color_control |= S_028808_MODE(V_028808_CB_DISABLE);

   radv_pipeline_compute_spi_color_formats(pipeline, pCreateInfo, &blend, info);

   pipeline->cb_color_control = cb_color_control;

   return blend;
}

static uint32_t
si_translate_fill(VkPolygonMode func)
{
   switch (func) {
   case VK_POLYGON_MODE_FILL:
      return V_028814_X_DRAW_TRIANGLES;
   case VK_POLYGON_MODE_LINE:
      return V_028814_X_DRAW_LINES;
   case VK_POLYGON_MODE_POINT:
      return V_028814_X_DRAW_POINTS;
   default:
      assert(0);
      return V_028814_X_DRAW_POINTS;
   }
}

static unsigned
radv_pipeline_color_samples( const struct radv_graphics_pipeline_info *info)
{
   if (info->color_att_samples && radv_pipeline_has_color_attachments(&info->ri)) {
      return info->color_att_samples;
   }

   return info->ms.raster_samples;
}

static unsigned
radv_pipeline_depth_samples(const struct radv_graphics_pipeline_info *info)
{
   if (info->ds_att_samples && radv_pipeline_has_ds_attachments(&info->ri)) {
      return info->ds_att_samples;
   }

   return info->ms.raster_samples;
}

static uint8_t
radv_pipeline_get_ps_iter_samples(const struct radv_graphics_pipeline_info *info)
{
   uint32_t ps_iter_samples = 1;
   uint32_t num_samples = radv_pipeline_color_samples(info);

   if (info->ms.sample_shading_enable) {
      ps_iter_samples = ceilf(info->ms.min_sample_shading * num_samples);
      ps_iter_samples = util_next_power_of_two(ps_iter_samples);
   }
   return ps_iter_samples;
}

static bool
radv_is_depth_write_enabled(const struct radv_depth_stencil_info *ds_info)
{
   return ds_info->depth_test_enable && ds_info->depth_write_enable &&
          ds_info->depth_compare_op != VK_COMPARE_OP_NEVER;
}

static bool
radv_writes_stencil(const struct radv_stencil_op_info *info)
{
   return info->write_mask &&
          (info->fail_op != VK_STENCIL_OP_KEEP || info->pass_op != VK_STENCIL_OP_KEEP ||
           info->depth_fail_op != VK_STENCIL_OP_KEEP);
}

static bool
radv_is_stencil_write_enabled(const struct radv_depth_stencil_info *ds_info)
{
   return ds_info->stencil_test_enable &&
          (radv_writes_stencil(&ds_info->front) || radv_writes_stencil(&ds_info->back));
}

static bool
radv_order_invariant_stencil_op(VkStencilOp op)
{
   /* REPLACE is normally order invariant, except when the stencil
    * reference value is written by the fragment shader. Tracking this
    * interaction does not seem worth the effort, so be conservative.
    */
   return op != VK_STENCIL_OP_INCREMENT_AND_CLAMP && op != VK_STENCIL_OP_DECREMENT_AND_CLAMP &&
          op != VK_STENCIL_OP_REPLACE;
}

static bool
radv_order_invariant_stencil_state(const struct radv_stencil_op_info *info)
{
   /* Compute whether, assuming Z writes are disabled, this stencil state
    * is order invariant in the sense that the set of passing fragments as
    * well as the final stencil buffer result does not depend on the order
    * of fragments.
    */
   return !info->write_mask ||
          /* The following assumes that Z writes are disabled. */
          (info->compare_op == VK_COMPARE_OP_ALWAYS &&
           radv_order_invariant_stencil_op(info->pass_op) &&
           radv_order_invariant_stencil_op(info->depth_fail_op)) ||
          (info->compare_op == VK_COMPARE_OP_NEVER &&
           radv_order_invariant_stencil_op(info->fail_op));
}

static bool
radv_pipeline_has_dynamic_ds_states(const struct radv_graphics_pipeline *pipeline)
{
   return !!(pipeline->dynamic_states & (RADV_DYNAMIC_DEPTH_TEST_ENABLE |
                                         RADV_DYNAMIC_DEPTH_WRITE_ENABLE |
                                         RADV_DYNAMIC_DEPTH_COMPARE_OP |
                                         RADV_DYNAMIC_STENCIL_TEST_ENABLE |
                                         RADV_DYNAMIC_STENCIL_WRITE_MASK |
                                         RADV_DYNAMIC_STENCIL_OP));
}

static bool
radv_pipeline_out_of_order_rast(struct radv_graphics_pipeline *pipeline,
                                const struct radv_blend_state *blend,
                                const struct radv_graphics_pipeline_info *info)
{
   unsigned colormask = blend->cb_target_enabled_4bit;

   if (!pipeline->base.device->physical_device->out_of_order_rast_allowed)
      return false;

   /* Be conservative if a logic operation is enabled with color buffers. */
   if (colormask && info->cb.logic_op_enable)
      return false;

   /* Be conservative if an extended dynamic depth/stencil state is
    * enabled because the driver can't update out-of-order rasterization
    * dynamically.
    */
   if (radv_pipeline_has_dynamic_ds_states(pipeline))
      return false;

   /* Default depth/stencil invariance when no attachment is bound. */
   struct radv_dsa_order_invariance dsa_order_invariant = {.zs = true, .pass_set = true};

   bool has_stencil = info->ri.stencil_att_format != VK_FORMAT_UNDEFINED;
   struct radv_dsa_order_invariance order_invariance[2];
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];

   /* Compute depth/stencil order invariance in order to know if
    * it's safe to enable out-of-order.
    */
   bool zfunc_is_ordered = info->ds.depth_compare_op == VK_COMPARE_OP_NEVER ||
                           info->ds.depth_compare_op == VK_COMPARE_OP_LESS ||
                           info->ds.depth_compare_op == VK_COMPARE_OP_LESS_OR_EQUAL ||
                           info->ds.depth_compare_op == VK_COMPARE_OP_GREATER ||
                           info->ds.depth_compare_op == VK_COMPARE_OP_GREATER_OR_EQUAL;
   bool depth_write_enabled = radv_is_depth_write_enabled(&info->ds);
   bool stencil_write_enabled = radv_is_stencil_write_enabled(&info->ds);
   bool ds_write_enabled = depth_write_enabled || stencil_write_enabled;

   bool nozwrite_and_order_invariant_stencil =
      !ds_write_enabled ||
      (!depth_write_enabled && radv_order_invariant_stencil_state(&info->ds.front) &&
       radv_order_invariant_stencil_state(&info->ds.back));

   order_invariance[1].zs = nozwrite_and_order_invariant_stencil ||
                            (!stencil_write_enabled && zfunc_is_ordered);
   order_invariance[0].zs = !depth_write_enabled || zfunc_is_ordered;

   order_invariance[1].pass_set =
      nozwrite_and_order_invariant_stencil ||
      (!stencil_write_enabled &&
       (info->ds.depth_compare_op == VK_COMPARE_OP_ALWAYS ||
        info->ds.depth_compare_op == VK_COMPARE_OP_NEVER));
   order_invariance[0].pass_set =
      !depth_write_enabled ||
      (info->ds.depth_compare_op == VK_COMPARE_OP_ALWAYS ||
       info->ds.depth_compare_op == VK_COMPARE_OP_NEVER);

   dsa_order_invariant = order_invariance[has_stencil];
   if (!dsa_order_invariant.zs)
      return false;

   /* The set of PS invocations is always order invariant,
    * except when early Z/S tests are requested.
    */
   if (ps && ps->info.ps.writes_memory && ps->info.ps.early_fragment_test &&
       !dsa_order_invariant.pass_set)
      return false;

   /* Determine if out-of-order rasterization should be disabled when occlusion queries are used. */
   pipeline->disable_out_of_order_rast_for_occlusion = !dsa_order_invariant.pass_set;

   /* No color buffers are enabled for writing. */
   if (!colormask)
      return true;

   unsigned blendmask = colormask & blend->blend_enable_4bit;

   if (blendmask) {
      /* Only commutative blending. */
      if (blendmask & ~blend->commutative_4bit)
         return false;

      if (!dsa_order_invariant.pass_set)
         return false;
   }

   if (colormask & ~blendmask)
      return false;

   return true;
}

static void
radv_pipeline_init_multisample_state(struct radv_graphics_pipeline *pipeline,
                                     const struct radv_blend_state *blend,
                                     const struct radv_graphics_pipeline_info *info,
                                     unsigned rast_prim)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_multisample_state *ms = &pipeline->ms;
   unsigned num_tile_pipes = pdevice->rad_info.num_tile_pipes;
   const VkConservativeRasterizationModeEXT mode = info->rs.conservative_mode;
   bool out_of_order_rast = false;
   int ps_iter_samples = 1;

   ms->num_samples = info->ms.raster_samples;

   /* From the Vulkan 1.1.129 spec, 26.7. Sample Shading:
    *
    * "Sample shading is enabled for a graphics pipeline:
    *
    * - If the interface of the fragment shader entry point of the
    *   graphics pipeline includes an input variable decorated
    *   with SampleId or SamplePosition. In this case
    *   minSampleShadingFactor takes the value 1.0.
    * - Else if the sampleShadingEnable member of the
    *   VkPipelineMultisampleStateCreateInfo structure specified
    *   when creating the graphics pipeline is set to VK_TRUE. In
    *   this case minSampleShadingFactor takes the value of
    *   VkPipelineMultisampleStateCreateInfo::minSampleShading.
    *
    * Otherwise, sample shading is considered disabled."
    */
   if (pipeline->base.shaders[MESA_SHADER_FRAGMENT]->info.ps.uses_sample_shading) {
      ps_iter_samples = ms->num_samples;
   } else {
      ps_iter_samples = radv_pipeline_get_ps_iter_samples(info);
   }

   if (info->rs.order == VK_RASTERIZATION_ORDER_RELAXED_AMD) {
      /* Out-of-order rasterization is explicitly enabled by the
       * application.
       */
      out_of_order_rast = true;
   } else {
      /* Determine if the driver can enable out-of-order
       * rasterization internally.
       */
      out_of_order_rast = radv_pipeline_out_of_order_rast(pipeline, blend, info);
   }

   ms->pa_sc_aa_config = 0;
   ms->db_eqaa = S_028804_HIGH_QUALITY_INTERSECTIONS(1) | S_028804_INCOHERENT_EQAA_READS(1) |
                 S_028804_INTERPOLATE_COMP_Z(1) | S_028804_STATIC_ANCHOR_ASSOCIATIONS(1);

   /* Adjust MSAA state if conservative rasterization is enabled. */
   if (mode != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
      ms->pa_sc_aa_config |= S_028BE0_AA_MASK_CENTROID_DTMN(1);

      ms->db_eqaa |=
         S_028804_ENABLE_POSTZ_OVERRASTERIZATION(1) | S_028804_OVERRASTERIZATION_AMOUNT(4);
   }

   ms->pa_sc_mode_cntl_1 =
      S_028A4C_WALK_FENCE_ENABLE(1) | // TODO linear dst fixes
      S_028A4C_WALK_FENCE_SIZE(num_tile_pipes == 2 ? 2 : 3) |
      S_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(out_of_order_rast) |
      S_028A4C_OUT_OF_ORDER_WATER_MARK(0x7) |
      /* always 1: */
      S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(1) | S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(1) |
      S_028A4C_TILE_WALK_ORDER_ENABLE(1) | S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(1) |
      S_028A4C_FORCE_EOV_CNTDWN_ENABLE(1) | S_028A4C_FORCE_EOV_REZ_ENABLE(1);
   ms->pa_sc_mode_cntl_0 = S_028A48_ALTERNATE_RBS_PER_TILE(pdevice->rad_info.gfx_level >= GFX9) |
                           S_028A48_VPORT_SCISSOR_ENABLE(1) |
                           S_028A48_LINE_STIPPLE_ENABLE(info->rs.stippled_line_enable);

   if (info->rs.line_raster_mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT &&
       radv_rast_prim_is_line(rast_prim)) {
      /* From the Vulkan spec 1.3.221:
       *
       * "When Bresenham lines are being rasterized, sample locations may all be treated as being at
       * the pixel center (this may affect attribute and depth interpolation)."
       *
       * "One consequence of this is that Bresenham lines cover the same pixels regardless of the
       * number of rasterization samples, and cover all samples in those pixels (unless masked out
       * or killed)."
       */
      ms->num_samples = 1;
   }

   if (ms->num_samples > 1) {
      uint32_t z_samples = radv_pipeline_depth_samples(info);
      unsigned log_samples = util_logbase2(ms->num_samples);
      unsigned log_z_samples = util_logbase2(z_samples);
      unsigned log_ps_iter_samples = util_logbase2(ps_iter_samples);
      ms->pa_sc_mode_cntl_0 |= S_028A48_MSAA_ENABLE(1);
      ms->db_eqaa |= S_028804_MAX_ANCHOR_SAMPLES(log_z_samples) |
                     S_028804_PS_ITER_SAMPLES(log_ps_iter_samples) |
                     S_028804_MASK_EXPORT_NUM_SAMPLES(log_samples) |
                     S_028804_ALPHA_TO_MASK_NUM_SAMPLES(log_samples);
      ms->pa_sc_aa_config |=
         S_028BE0_MSAA_NUM_SAMPLES(log_samples) |
         S_028BE0_MAX_SAMPLE_DIST(radv_get_default_max_sample_dist(log_samples)) |
         S_028BE0_MSAA_EXPOSED_SAMPLES(log_samples) | /* CM_R_028BE0_PA_SC_AA_CONFIG */
         S_028BE0_COVERED_CENTROID_IS_CENTER(pdevice->rad_info.gfx_level >= GFX10_3);
      ms->pa_sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(ps_iter_samples > 1);
      if (ps_iter_samples > 1)
         pipeline->spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(2);
   }

   ms->pa_sc_aa_mask[0] = info->ms.sample_mask | ((uint32_t)info->ms.sample_mask << 16);
   ms->pa_sc_aa_mask[1] = info->ms.sample_mask | ((uint32_t)info->ms.sample_mask << 16);
}

static void
gfx103_pipeline_init_vrs_state(struct radv_graphics_pipeline *pipeline,
                               const struct radv_graphics_pipeline_info *info)
{
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   struct radv_multisample_state *ms = &pipeline->ms;
   struct radv_vrs_state *vrs = &pipeline->vrs;

   if (info->ms.sample_shading_enable ||
       ps->info.ps.uses_sample_shading || ps->info.ps.reads_sample_mask_in) {
      /* Disable VRS and use the rates from PS_ITER_SAMPLES if:
       *
       * 1) sample shading is enabled or per-sample interpolation is
       *    used by the fragment shader
       * 2) the fragment shader reads gl_SampleMaskIn because the
       *    16-bit sample coverage mask isn't enough for MSAA8x and
       *    2x2 coarse shading isn't enough.
       */
      vrs->pa_cl_vrs_cntl = S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE);

      /* Make sure sample shading is enabled even if only MSAA1x is
       * used because the SAMPLE_ITER combiner is in passthrough
       * mode if PS_ITER_SAMPLE is 0, and it uses the per-draw rate.
       * The default VRS rate when sample shading is enabled is 1x1.
       */
      if (!G_028A4C_PS_ITER_SAMPLE(ms->pa_sc_mode_cntl_1))
         ms->pa_sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(1);
   } else {
      vrs->pa_cl_vrs_cntl = S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_PASSTHRU);
   }
}

static uint32_t
si_conv_tess_prim_to_gs_out(enum tess_primitive_mode prim)
{
   switch (prim) {
   case TESS_PRIMITIVE_TRIANGLES:
   case TESS_PRIMITIVE_QUADS:
      return V_028A6C_TRISTRIP;
   case TESS_PRIMITIVE_ISOLINES:
      return V_028A6C_LINESTRIP;
   default:
      assert(0);
      return 0;
   }
}

static uint32_t
si_conv_gl_prim_to_gs_out(unsigned gl_prim)
{
   switch (gl_prim) {
   case SHADER_PRIM_POINTS:
      return V_028A6C_POINTLIST;
   case SHADER_PRIM_LINES:
   case SHADER_PRIM_LINE_STRIP:
   case SHADER_PRIM_LINES_ADJACENCY:
      return V_028A6C_LINESTRIP;

   case SHADER_PRIM_TRIANGLES:
   case SHADER_PRIM_TRIANGLE_STRIP_ADJACENCY:
   case SHADER_PRIM_TRIANGLE_STRIP:
   case SHADER_PRIM_QUADS:
      return V_028A6C_TRISTRIP;
   default:
      assert(0);
      return 0;
   }
}

static uint64_t
radv_dynamic_state_mask(VkDynamicState state)
{
   switch (state) {
   case VK_DYNAMIC_STATE_VIEWPORT:
   case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
      return RADV_DYNAMIC_VIEWPORT;
   case VK_DYNAMIC_STATE_SCISSOR:
   case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
      return RADV_DYNAMIC_SCISSOR;
   case VK_DYNAMIC_STATE_LINE_WIDTH:
      return RADV_DYNAMIC_LINE_WIDTH;
   case VK_DYNAMIC_STATE_DEPTH_BIAS:
      return RADV_DYNAMIC_DEPTH_BIAS;
   case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      return RADV_DYNAMIC_BLEND_CONSTANTS;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      return RADV_DYNAMIC_DEPTH_BOUNDS;
   case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      return RADV_DYNAMIC_STENCIL_COMPARE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      return RADV_DYNAMIC_STENCIL_WRITE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      return RADV_DYNAMIC_STENCIL_REFERENCE;
   case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT:
      return RADV_DYNAMIC_DISCARD_RECTANGLE;
   case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT:
      return RADV_DYNAMIC_SAMPLE_LOCATIONS;
   case VK_DYNAMIC_STATE_LINE_STIPPLE_EXT:
      return RADV_DYNAMIC_LINE_STIPPLE;
   case VK_DYNAMIC_STATE_CULL_MODE:
      return RADV_DYNAMIC_CULL_MODE;
   case VK_DYNAMIC_STATE_FRONT_FACE:
      return RADV_DYNAMIC_FRONT_FACE;
   case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
      return RADV_DYNAMIC_PRIMITIVE_TOPOLOGY;
   case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
      return RADV_DYNAMIC_DEPTH_TEST_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
      return RADV_DYNAMIC_DEPTH_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP:
      return RADV_DYNAMIC_DEPTH_COMPARE_OP;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
      return RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
      return RADV_DYNAMIC_STENCIL_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_OP:
      return RADV_DYNAMIC_STENCIL_OP;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
      return RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE;
   case VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR:
      return RADV_DYNAMIC_FRAGMENT_SHADING_RATE;
   case VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT:
      return RADV_DYNAMIC_PATCH_CONTROL_POINTS;
   case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
      return RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
      return RADV_DYNAMIC_DEPTH_BIAS_ENABLE;
   case VK_DYNAMIC_STATE_LOGIC_OP_EXT:
      return RADV_DYNAMIC_LOGIC_OP;
   case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
      return RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE;
   case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
      return RADV_DYNAMIC_COLOR_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:
      return RADV_DYNAMIC_VERTEX_INPUT;
   default:
      unreachable("Unhandled dynamic state");
   }
}

static bool
radv_pipeline_is_blend_enabled(const struct radv_graphics_pipeline *pipeline,
                               const struct radv_color_blend_info *cb_info)
{
   for (uint32_t i = 0; i < cb_info->att_count; i++) {
      if (cb_info->att[i].color_write_mask && cb_info->att[i].blend_enable)
         return true;
   }

   return false;
}

static uint64_t
radv_pipeline_needed_dynamic_state(const struct radv_graphics_pipeline *pipeline,
                                   const struct radv_graphics_pipeline_info *info)
{
   bool has_color_att = radv_pipeline_has_color_attachments(&info->ri);
   bool raster_enabled = !info->rs.discard_enable ||
                         (pipeline->dynamic_states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE);
   uint64_t states = RADV_DYNAMIC_ALL;

   /* Disable dynamic states that are useless to mesh shading. */
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      if (!raster_enabled)
         return RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE;

      states &= ~(RADV_DYNAMIC_VERTEX_INPUT | RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE |
                  RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE | RADV_DYNAMIC_PRIMITIVE_TOPOLOGY);
   }

   /* If rasterization is disabled we do not care about any of the
    * dynamic states, since they are all rasterization related only,
    * except primitive topology, primitive restart enable, vertex
    * binding stride and rasterization discard itself.
    */
   if (!raster_enabled) {
      return RADV_DYNAMIC_PRIMITIVE_TOPOLOGY | RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE |
             RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE | RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE |
             RADV_DYNAMIC_VERTEX_INPUT;
   }

   if (!info->rs.depth_bias_enable &&
       !(pipeline->dynamic_states & RADV_DYNAMIC_DEPTH_BIAS_ENABLE))
      states &= ~RADV_DYNAMIC_DEPTH_BIAS;

   if (!info->ds.depth_bounds_test_enable &&
       !(pipeline->dynamic_states & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE))
      states &= ~RADV_DYNAMIC_DEPTH_BOUNDS;

   if (!info->ds.stencil_test_enable &&
       !(pipeline->dynamic_states & RADV_DYNAMIC_STENCIL_TEST_ENABLE))
      states &= ~(RADV_DYNAMIC_STENCIL_COMPARE_MASK | RADV_DYNAMIC_STENCIL_WRITE_MASK |
                  RADV_DYNAMIC_STENCIL_REFERENCE | RADV_DYNAMIC_STENCIL_OP);

   if (!info->dr.count)
      states &= ~RADV_DYNAMIC_DISCARD_RECTANGLE;

   if (!info->ms.sample_locs_enable)
      states &= ~RADV_DYNAMIC_SAMPLE_LOCATIONS;

   if (!info->rs.stippled_line_enable)
      states &= ~RADV_DYNAMIC_LINE_STIPPLE;

   if (!radv_is_vrs_enabled(pipeline, info))
      states &= ~RADV_DYNAMIC_FRAGMENT_SHADING_RATE;

   if (!has_color_att || !radv_pipeline_is_blend_enabled(pipeline, &info->cb))
      states &= ~RADV_DYNAMIC_BLEND_CONSTANTS;

   if (!has_color_att)
      states &= ~RADV_DYNAMIC_COLOR_WRITE_ENABLE;

   return states;
}

static struct radv_ia_multi_vgt_param_helpers
radv_compute_ia_multi_vgt_param_helpers(struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_ia_multi_vgt_param_helpers ia_multi_vgt_param = {0};

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL))
      ia_multi_vgt_param.primgroup_size =
         pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.num_tess_patches;
   else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
      ia_multi_vgt_param.primgroup_size = 64;
   else
      ia_multi_vgt_param.primgroup_size = 128; /* recommended without a GS */

   /* GS requirement. */
   ia_multi_vgt_param.partial_es_wave = false;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && pdevice->rad_info.gfx_level <= GFX8)
      if (SI_GS_PER_ES / ia_multi_vgt_param.primgroup_size >= pdevice->gs_table_depth - 3)
         ia_multi_vgt_param.partial_es_wave = true;

   ia_multi_vgt_param.ia_switch_on_eoi = false;
   if (pipeline->base.shaders[MESA_SHADER_FRAGMENT]->info.ps.prim_id_input)
      ia_multi_vgt_param.ia_switch_on_eoi = true;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.uses_prim_id)
      ia_multi_vgt_param.ia_switch_on_eoi = true;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      /* SWITCH_ON_EOI must be set if PrimID is used. */
      if (pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.uses_prim_id ||
          radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL)->info.uses_prim_id)
         ia_multi_vgt_param.ia_switch_on_eoi = true;
   }

   ia_multi_vgt_param.partial_vs_wave = false;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      /* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
      if ((pdevice->rad_info.family == CHIP_TAHITI ||
           pdevice->rad_info.family == CHIP_PITCAIRN ||
           pdevice->rad_info.family == CHIP_BONAIRE) &&
          radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
         ia_multi_vgt_param.partial_vs_wave = true;
      /* Needed for 028B6C_DISTRIBUTION_MODE != 0 */
      if (pdevice->rad_info.has_distributed_tess) {
         if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
            if (pdevice->rad_info.gfx_level <= GFX8)
               ia_multi_vgt_param.partial_es_wave = true;
         } else {
            ia_multi_vgt_param.partial_vs_wave = true;
         }
      }
   }

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      /* On these chips there is the possibility of a hang if the
       * pipeline uses a GS and partial_vs_wave is not set.
       *
       * This mostly does not hit 4-SE chips, as those typically set
       * ia_switch_on_eoi and then partial_vs_wave is set for pipelines
       * with GS due to another workaround.
       *
       * Reproducer: https://bugs.freedesktop.org/show_bug.cgi?id=109242
       */
      if (pdevice->rad_info.family == CHIP_TONGA ||
          pdevice->rad_info.family == CHIP_FIJI ||
          pdevice->rad_info.family == CHIP_POLARIS10 ||
          pdevice->rad_info.family == CHIP_POLARIS11 ||
          pdevice->rad_info.family == CHIP_POLARIS12 ||
          pdevice->rad_info.family == CHIP_VEGAM) {
         ia_multi_vgt_param.partial_vs_wave = true;
      }
   }

   ia_multi_vgt_param.base =
      S_028AA8_PRIMGROUP_SIZE(ia_multi_vgt_param.primgroup_size - 1) |
      /* The following field was moved to VGT_SHADER_STAGES_EN in GFX9. */
      S_028AA8_MAX_PRIMGRP_IN_WAVE(pdevice->rad_info.gfx_level == GFX8 ? 2 : 0) |
      S_030960_EN_INST_OPT_BASIC(pdevice->rad_info.gfx_level >= GFX9) |
      S_030960_EN_INST_OPT_ADV(pdevice->rad_info.gfx_level >= GFX9);

   return ia_multi_vgt_param;
}

static uint32_t
radv_get_attrib_stride(const VkPipelineVertexInputStateCreateInfo *vi, uint32_t attrib_binding)
{
   for (uint32_t i = 0; i < vi->vertexBindingDescriptionCount; i++) {
      const VkVertexInputBindingDescription *input_binding = &vi->pVertexBindingDescriptions[i];

      if (input_binding->binding == attrib_binding)
         return input_binding->stride;
   }

   return 0;
}

static struct radv_vertex_input_info
radv_pipeline_init_vertex_input_info(struct radv_graphics_pipeline *pipeline,
                                     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const VkPipelineVertexInputStateCreateInfo *vi = pCreateInfo->pVertexInputState;
   struct radv_vertex_input_info info = {0};

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_VERTEX_INPUT)) {
      /* Vertex input */
      const VkPipelineVertexInputDivisorStateCreateInfoEXT *divisor_state =
         vk_find_struct_const(vi->pNext, PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT);

      uint32_t binding_input_rate = 0;
      uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];
      for (unsigned i = 0; i < vi->vertexBindingDescriptionCount; ++i) {
         const VkVertexInputBindingDescription *desc = &vi->pVertexBindingDescriptions[i];

         if (desc->inputRate) {
            unsigned binding = vi->pVertexBindingDescriptions[i].binding;
            binding_input_rate |= 1u << binding;
            instance_rate_divisors[binding] = 1;
         }

         info.binding_stride[desc->binding] = desc->stride;
      }

      if (divisor_state) {
         for (unsigned i = 0; i < divisor_state->vertexBindingDivisorCount; ++i) {
            instance_rate_divisors[divisor_state->pVertexBindingDivisors[i].binding] =
               divisor_state->pVertexBindingDivisors[i].divisor;
         }
      }

      for (unsigned i = 0; i < vi->vertexAttributeDescriptionCount; ++i) {
         const VkVertexInputAttributeDescription *desc = &vi->pVertexAttributeDescriptions[i];
         const struct util_format_description *format_desc;
         unsigned location = desc->location;
         unsigned binding = desc->binding;
         unsigned num_format, data_format;
         bool post_shuffle;

         if (binding_input_rate & (1u << binding)) {
            info.instance_rate_inputs |= 1u << location;
            info.instance_rate_divisors[location] = instance_rate_divisors[binding];
         }

         format_desc = vk_format_description(desc->format);
         radv_translate_vertex_format(pdevice, desc->format, format_desc, &data_format, &num_format,
                                      &post_shuffle, &info.vertex_alpha_adjust[location]);

         info.vertex_attribute_formats[location] = data_format | (num_format << 4);
         info.vertex_attribute_bindings[location] = desc->binding;
         info.vertex_attribute_offsets[location] = desc->offset;

         const struct ac_data_format_info *dfmt_info = ac_get_data_format_info(data_format);
         unsigned attrib_align =
            dfmt_info->chan_byte_size ? dfmt_info->chan_byte_size : dfmt_info->element_size;

         /* If desc->offset is misaligned, then the buffer offset must be too. Just
          * skip updating vertex_binding_align in this case.
          */
         if (desc->offset % attrib_align == 0)
            info.vertex_binding_align[desc->binding] =
               MAX2(info.vertex_binding_align[desc->binding], attrib_align);

         if (!(pipeline->dynamic_states & RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE)) {
            /* From the Vulkan spec 1.2.157:
             *
             * "If the bound pipeline state object was created
             *  with the
             *  VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE
             *  dynamic state enabled then pStrides[i] specifies
             *  the distance in bytes between two consecutive
             *  elements within the corresponding buffer. In this
             *  case the VkVertexInputBindingDescription::stride
             *  state from the pipeline state object is ignored."
             *
             * Make sure the vertex attribute stride is zero to
             * avoid computing a wrong offset if it's initialized
             * to something else than zero.
             */
            info.vertex_attribute_strides[location] = radv_get_attrib_stride(vi, desc->binding);
         }

         if (post_shuffle)
            info.vertex_post_shuffle |= 1 << location;

         uint32_t end = desc->offset + vk_format_get_blocksize(desc->format);
         info.attrib_ends[desc->location] = end;
         if (info.binding_stride[desc->binding])
            info.attrib_index_offset[desc->location] =
               desc->offset / info.binding_stride[desc->binding];
         info.attrib_bindings[desc->location] = desc->binding;
      }
   }

   return info;
}

static struct radv_input_assembly_info
radv_pipeline_init_input_assembly_info(struct radv_graphics_pipeline *pipeline,
                                       const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineInputAssemblyStateCreateInfo *ia = pCreateInfo->pInputAssemblyState;
   struct radv_input_assembly_info info = {0};

   info.primitive_topology = si_translate_prim(ia->topology);
   info.primitive_restart_enable = !!ia->primitiveRestartEnable;

   return info;
}

static struct radv_tessellation_info
radv_pipeline_init_tessellation_info(struct radv_graphics_pipeline *pipeline,
                                     const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineTessellationStateCreateInfo *ts = pCreateInfo->pTessellationState;
   const VkShaderStageFlagBits tess_stages = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                             VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
   struct radv_tessellation_info info = {0};

   if ((pipeline->active_stages & tess_stages) == tess_stages) {
      info.patch_control_points = ts->patchControlPoints;

      const VkPipelineTessellationDomainOriginStateCreateInfo *domain_origin_state =
         vk_find_struct_const(ts->pNext, PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO);
      if (domain_origin_state) {
         info.domain_origin = domain_origin_state->domainOrigin;
      }
   }

   return info;
}

static struct radv_viewport_info
radv_pipeline_init_viewport_info(struct radv_graphics_pipeline *pipeline,
                                 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineViewportStateCreateInfo *vp = pCreateInfo->pViewportState;
   struct radv_viewport_info info = {0};

   if (radv_is_raster_enabled(pipeline, pCreateInfo)) {
      if (!(pipeline->dynamic_states & RADV_DYNAMIC_VIEWPORT)) {
         typed_memcpy(info.viewports, vp->pViewports, vp->viewportCount);
      }
      info.viewport_count = vp->viewportCount;

      if (!(pipeline->dynamic_states & RADV_DYNAMIC_SCISSOR)) {
         typed_memcpy(info.scissors, vp->pScissors, vp->scissorCount);
      }
      info.scissor_count = vp->scissorCount;

      const VkPipelineViewportDepthClipControlCreateInfoEXT *depth_clip_control =
         vk_find_struct_const(vp->pNext, PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT);
      if (depth_clip_control) {
         info.negative_one_to_one = !!depth_clip_control->negativeOneToOne;
      }
   }

   return info;
}

static struct radv_rasterization_info
radv_pipeline_init_rasterization_info(struct radv_graphics_pipeline *pipeline,
                                      const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineRasterizationStateCreateInfo *rs = pCreateInfo->pRasterizationState;
   struct radv_rasterization_info info = {0};

   info.discard_enable = rs->rasterizerDiscardEnable;
   info.front_face = rs->frontFace;
   info.cull_mode = rs->cullMode;
   info.polygon_mode = si_translate_fill(rs->polygonMode);
   info.depth_bias_enable = rs->depthBiasEnable;
   info.depth_clamp_enable = rs->depthClampEnable;
   info.line_width = rs->lineWidth;
   info.depth_bias_constant_factor = rs->depthBiasConstantFactor;
   info.depth_bias_clamp = rs->depthBiasClamp;
   info.depth_bias_slope_factor = rs->depthBiasSlopeFactor;
   info.depth_clip_disable = rs->depthClampEnable;

   const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *provoking_vtx_info =
      vk_find_struct_const(rs->pNext, PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT);
   if (provoking_vtx_info &&
       provoking_vtx_info->provokingVertexMode == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT) {
      info.provoking_vtx_last = true;
   }

   const VkPipelineRasterizationConservativeStateCreateInfoEXT *conservative_raster =
      vk_find_struct_const(rs->pNext, PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT);
   if (conservative_raster) {
      info.conservative_mode = conservative_raster->conservativeRasterizationMode;
   }

   const VkPipelineRasterizationLineStateCreateInfoEXT *rast_line_info =
      vk_find_struct_const(rs->pNext, PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT);
   if (rast_line_info) {
      info.stippled_line_enable = rast_line_info->stippledLineEnable;
      info.line_raster_mode = rast_line_info->lineRasterizationMode;
      info.line_stipple_factor = rast_line_info->lineStippleFactor;
      info.line_stipple_pattern = rast_line_info->lineStipplePattern;
   }

   const VkPipelineRasterizationDepthClipStateCreateInfoEXT *depth_clip_state =
      vk_find_struct_const(rs->pNext, PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT);
   if (depth_clip_state) {
      info.depth_clip_disable = !depth_clip_state->depthClipEnable;
   }

   const VkPipelineRasterizationStateRasterizationOrderAMD *raster_order =
      vk_find_struct_const(rs->pNext, PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD);
   if (raster_order) {
      info.order = raster_order->rasterizationOrder;
   }

   return info;
}

static struct radv_discard_rectangle_info
radv_pipeline_init_discard_rectangle_info(struct radv_graphics_pipeline *pipeline,
                                          const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineDiscardRectangleStateCreateInfoEXT *discard_rectangle_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_DISCARD_RECTANGLE_STATE_CREATE_INFO_EXT);
   struct radv_discard_rectangle_info info = {0};

   if (discard_rectangle_info) {
      info.mode = discard_rectangle_info->discardRectangleMode;
      if (!(pipeline->dynamic_states & RADV_DYNAMIC_DISCARD_RECTANGLE)) {
         typed_memcpy(info.rects, discard_rectangle_info->pDiscardRectangles,
                      discard_rectangle_info->discardRectangleCount);
      }
      info.count = discard_rectangle_info->discardRectangleCount;
   }

   return info;
}

static struct radv_multisample_info
radv_pipeline_init_multisample_info(struct radv_graphics_pipeline *pipeline,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineMultisampleStateCreateInfo *ms = pCreateInfo->pMultisampleState;
   struct radv_multisample_info info = {0};

   if (radv_is_raster_enabled(pipeline, pCreateInfo)) {
      info.raster_samples = ms->rasterizationSamples;
      info.sample_shading_enable = ms->sampleShadingEnable;
      info.min_sample_shading = ms->minSampleShading;
      info.alpha_to_coverage_enable = ms->alphaToCoverageEnable;
      if (ms->pSampleMask) {
         info.sample_mask = ms->pSampleMask[0] & 0xffff;
      } else {
         info.sample_mask = 0xffff;
      }

      const VkPipelineSampleLocationsStateCreateInfoEXT *sample_location_info =
         vk_find_struct_const(ms->pNext, PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);
      if (sample_location_info) {
         /* If sampleLocationsEnable is VK_FALSE, the default sample locations are used and the
          * values specified in sampleLocationsInfo are ignored.
          */
         info.sample_locs_enable = sample_location_info->sampleLocationsEnable;
         if (sample_location_info->sampleLocationsEnable) {
            const VkSampleLocationsInfoEXT *pSampleLocationsInfo =
               &sample_location_info->sampleLocationsInfo;
            assert(pSampleLocationsInfo->sampleLocationsCount <= MAX_SAMPLE_LOCATIONS);

            info.sample_locs_per_pixel = pSampleLocationsInfo->sampleLocationsPerPixel;
            info.sample_locs_grid_size = pSampleLocationsInfo->sampleLocationGridSize;
            for (uint32_t i = 0; i < pSampleLocationsInfo->sampleLocationsCount; i++) {
               info.sample_locs[i] = pSampleLocationsInfo->pSampleLocations[i];
            }
            info.sample_locs_count = pSampleLocationsInfo->sampleLocationsCount;
         }
      }
   } else {
      info.raster_samples = VK_SAMPLE_COUNT_1_BIT;
   }

   return info;
}

static struct radv_depth_stencil_info
radv_pipeline_init_depth_stencil_info(struct radv_graphics_pipeline *pipeline,
                                      const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineDepthStencilStateCreateInfo *ds = pCreateInfo->pDepthStencilState;
   const VkPipelineRenderingCreateInfo *ri =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_RENDERING_CREATE_INFO);
   struct radv_depth_stencil_info info = {0};

   if (radv_is_raster_enabled(pipeline, pCreateInfo) &&
       (ri->depthAttachmentFormat != VK_FORMAT_UNDEFINED ||
        ri->stencilAttachmentFormat != VK_FORMAT_UNDEFINED)) {
      info.depth_bounds_test_enable = ds->depthBoundsTestEnable;
      info.depth_bounds.min = ds->minDepthBounds;
      info.depth_bounds.max = ds->maxDepthBounds;
      info.stencil_test_enable = ds->stencilTestEnable;
      info.front.fail_op = ds->front.failOp;
      info.front.pass_op = ds->front.passOp;
      info.front.depth_fail_op = ds->front.depthFailOp;
      info.front.compare_op = ds->front.compareOp;
      info.front.compare_mask = ds->front.compareMask;
      info.front.write_mask = ds->front.writeMask;
      info.front.reference = ds->front.reference;
      info.back.fail_op = ds->back.failOp;
      info.back.pass_op = ds->back.passOp;
      info.back.depth_fail_op = ds->back.depthFailOp;
      info.back.compare_op = ds->back.compareOp;
      info.back.compare_mask = ds->back.compareMask;
      info.back.write_mask = ds->back.writeMask;
      info.back.reference = ds->back.reference;
      info.depth_test_enable = ds->depthTestEnable;
      info.depth_write_enable = ds->depthWriteEnable;
      info.depth_compare_op = ds->depthCompareOp;
   }

   return info;
}

static struct radv_rendering_info
radv_pipeline_init_rendering_info(struct radv_graphics_pipeline *pipeline,
                                  const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineRenderingCreateInfo *ri =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_RENDERING_CREATE_INFO);
   struct radv_rendering_info info = {0};

   info.view_mask = ri->viewMask;
   for (uint32_t i = 0; i < ri->colorAttachmentCount; i++) {
      info.color_att_formats[i] = ri->pColorAttachmentFormats[i];
   }
   info.color_att_count = ri->colorAttachmentCount;
   info.depth_att_format = ri->depthAttachmentFormat;
   info.stencil_att_format = ri->stencilAttachmentFormat;

   return info;
}

static struct radv_color_blend_info
radv_pipeline_init_color_blend_info(struct radv_graphics_pipeline *pipeline,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const VkPipelineColorBlendStateCreateInfo *cb = pCreateInfo->pColorBlendState;
   const VkPipelineRenderingCreateInfo *ri =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_RENDERING_CREATE_INFO);
   struct radv_color_blend_info info = {0};
   bool has_color_att = false;

   for (uint32_t i = 0; i < ri->colorAttachmentCount; ++i) {
      if (ri->pColorAttachmentFormats[i] != VK_FORMAT_UNDEFINED) {
         has_color_att = true;
         break;
      }
   }

   if (radv_is_raster_enabled(pipeline, pCreateInfo) && has_color_att) {
      for (uint32_t i = 0; i < cb->attachmentCount; i++) {
         const VkPipelineColorBlendAttachmentState *att = &cb->pAttachments[i];

         info.att[i].color_write_mask = att->colorWriteMask;
         info.att[i].blend_enable = att->blendEnable;
         info.att[i].color_blend_op = si_translate_blend_function(att->colorBlendOp);
         info.att[i].alpha_blend_op = si_translate_blend_function(att->alphaBlendOp);
         info.att[i].src_color_blend_factor =
            si_translate_blend_factor(pdevice->rad_info.gfx_level, att->srcColorBlendFactor);
         info.att[i].dst_color_blend_factor =
            si_translate_blend_factor(pdevice->rad_info.gfx_level, att->dstColorBlendFactor);
         info.att[i].src_alpha_blend_factor =
            si_translate_blend_factor(pdevice->rad_info.gfx_level, att->srcAlphaBlendFactor);
         info.att[i].dst_alpha_blend_factor =
            si_translate_blend_factor(pdevice->rad_info.gfx_level, att->dstAlphaBlendFactor);
      }
      info.att_count = cb->attachmentCount;

      for (uint32_t i = 0; i < 4; i++) {
         info.blend_constants[i] = cb->blendConstants[i];
      }

      info.logic_op_enable = cb->logicOpEnable;
      if (info.logic_op_enable)
         info.logic_op = si_translate_blend_logic_op(cb->logicOp);

      const VkPipelineColorWriteCreateInfoEXT *color_write_info =
         vk_find_struct_const(cb->pNext, PIPELINE_COLOR_WRITE_CREATE_INFO_EXT);
      if (color_write_info) {
         for (uint32_t i = 0; i < color_write_info->attachmentCount; i++) {
            info.color_write_enable |=
               color_write_info->pColorWriteEnables[i] ? (0xfu << (i * 4)) : 0;
         }
      } else {
         info.color_write_enable = 0xffffffffu;
      }
   }

   return info;
}

static struct radv_fragment_shading_rate_info
radv_pipeline_init_fragment_shading_rate_info(struct radv_graphics_pipeline *pipeline,
                                              const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineFragmentShadingRateStateCreateInfoKHR *shading_rate =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR);
   struct radv_fragment_shading_rate_info info = {0};

   if (shading_rate && !(pipeline->dynamic_states & RADV_DYNAMIC_FRAGMENT_SHADING_RATE)) {
      info.size = shading_rate->fragmentSize;
      for (int i = 0; i < 2; i++)
         info.combiner_ops[i] = shading_rate->combinerOps[i];
   } else {
      info.size = (VkExtent2D){ 1, 1 };
      info.combiner_ops[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
      info.combiner_ops[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
   }

   return info;
}

static struct radv_graphics_pipeline_info
radv_pipeline_init_graphics_info(struct radv_graphics_pipeline *pipeline,
                                 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   struct radv_graphics_pipeline_info info = {0};

   /* Vertex input interface structs have to be ignored if the pipeline includes a mesh shader. */
   if (!(pipeline->active_stages & VK_SHADER_STAGE_MESH_BIT_NV)) {
      info.vi = radv_pipeline_init_vertex_input_info(pipeline, pCreateInfo);
      info.ia = radv_pipeline_init_input_assembly_info(pipeline, pCreateInfo);
   }

   info.ts = radv_pipeline_init_tessellation_info(pipeline, pCreateInfo);
   info.vp = radv_pipeline_init_viewport_info(pipeline, pCreateInfo);
   info.rs = radv_pipeline_init_rasterization_info(pipeline, pCreateInfo);
   info.dr = radv_pipeline_init_discard_rectangle_info(pipeline, pCreateInfo);

   info.ms = radv_pipeline_init_multisample_info(pipeline, pCreateInfo);
   info.ds = radv_pipeline_init_depth_stencil_info(pipeline, pCreateInfo);
   info.ri = radv_pipeline_init_rendering_info(pipeline, pCreateInfo);
   info.cb = radv_pipeline_init_color_blend_info(pipeline, pCreateInfo);

   info.fsr = radv_pipeline_init_fragment_shading_rate_info(pipeline, pCreateInfo);

   /* VK_AMD_mixed_attachment_samples */
   const VkAttachmentSampleCountInfoAMD *sample_info =
      vk_find_struct_const(pCreateInfo->pNext, ATTACHMENT_SAMPLE_COUNT_INFO_AMD);
   if (sample_info) {
      for (uint32_t i = 0; i < sample_info->colorAttachmentCount; ++i) {
         if (info.ri.color_att_formats[i] != VK_FORMAT_UNDEFINED) {
            info.color_att_samples = MAX2(info.color_att_samples, sample_info->pColorAttachmentSamples[i]);
         }
      }
      info.ds_att_samples = sample_info->depthStencilAttachmentSamples;
   }

   return info;
}

static void
radv_pipeline_init_input_assembly_state(struct radv_graphics_pipeline *pipeline,
                                        const struct radv_graphics_pipeline_info *info)
{
   pipeline->ia_multi_vgt_param = radv_compute_ia_multi_vgt_param_helpers(pipeline);
}

static void
radv_pipeline_init_dynamic_state(struct radv_graphics_pipeline *pipeline,
                                 const struct radv_graphics_pipeline_info *info)
{
   uint64_t needed_states = radv_pipeline_needed_dynamic_state(pipeline, info);
   uint64_t states = needed_states;

   pipeline->dynamic_state = default_dynamic_state;
   pipeline->needed_dynamic_state = needed_states;

   states &= ~pipeline->dynamic_states;

   struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;

   if (needed_states & RADV_DYNAMIC_VIEWPORT) {
      dynamic->viewport.count = info->vp.viewport_count;
      if (states & RADV_DYNAMIC_VIEWPORT) {
         typed_memcpy(dynamic->viewport.viewports, info->vp.viewports, info->vp.viewport_count);
         for (unsigned i = 0; i < dynamic->viewport.count; i++)
            radv_get_viewport_xform(&dynamic->viewport.viewports[i],
                                    dynamic->viewport.xform[i].scale, dynamic->viewport.xform[i].translate);
      }
   }

   if (needed_states & RADV_DYNAMIC_SCISSOR) {
      dynamic->scissor.count = info->vp.scissor_count;
      if (states & RADV_DYNAMIC_SCISSOR) {
         typed_memcpy(dynamic->scissor.scissors, info->vp.scissors, info->vp.scissor_count);
      }
   }

   if (states & RADV_DYNAMIC_LINE_WIDTH) {
      dynamic->line_width = info->rs.line_width;
   }

   if (states & RADV_DYNAMIC_DEPTH_BIAS) {
      dynamic->depth_bias.bias = info->rs.depth_bias_constant_factor;
      dynamic->depth_bias.clamp = info->rs.depth_bias_clamp;
      dynamic->depth_bias.slope = info->rs.depth_bias_slope_factor;
   }

   /* Section 9.2 of the Vulkan 1.0.15 spec says:
    *
    *    pColorBlendState is [...] NULL if the pipeline has rasterization
    *    disabled or if the subpass of the render pass the pipeline is
    *    created against does not use any color attachments.
    */
   if (states & RADV_DYNAMIC_BLEND_CONSTANTS) {
      typed_memcpy(dynamic->blend_constants, info->cb.blend_constants, 4);
   }

   if (states & RADV_DYNAMIC_CULL_MODE) {
      dynamic->cull_mode = info->rs.cull_mode;
   }

   if (states & RADV_DYNAMIC_FRONT_FACE) {
      dynamic->front_face = info->rs.front_face;
   }

   if (states & RADV_DYNAMIC_PRIMITIVE_TOPOLOGY) {
      dynamic->primitive_topology = info->ia.primitive_topology;
   }

   /* If there is no depthstencil attachment, then don't read
    * pDepthStencilState. The Vulkan spec states that pDepthStencilState may
    * be NULL in this case. Even if pDepthStencilState is non-NULL, there is
    * no need to override the depthstencil defaults in
    * radv_pipeline::dynamic_state when there is no depthstencil attachment.
    *
    * Section 9.2 of the Vulkan 1.0.15 spec says:
    *
    *    pDepthStencilState is [...] NULL if the pipeline has rasterization
    *    disabled or if the subpass of the render pass the pipeline is created
    *    against does not use a depth/stencil attachment.
    */
   if (needed_states && radv_pipeline_has_ds_attachments(&info->ri)) {
      if (states & RADV_DYNAMIC_DEPTH_BOUNDS) {
         dynamic->depth_bounds.min = info->ds.depth_bounds.min;
         dynamic->depth_bounds.max = info->ds.depth_bounds.max;
      }

      if (states & RADV_DYNAMIC_STENCIL_COMPARE_MASK) {
         dynamic->stencil_compare_mask.front = info->ds.front.compare_mask;
         dynamic->stencil_compare_mask.back = info->ds.back.compare_mask;
      }

      if (states & RADV_DYNAMIC_STENCIL_WRITE_MASK) {
         dynamic->stencil_write_mask.front = info->ds.front.write_mask;
         dynamic->stencil_write_mask.back = info->ds.back.write_mask;
      }

      if (states & RADV_DYNAMIC_STENCIL_REFERENCE) {
         dynamic->stencil_reference.front = info->ds.front.reference;
         dynamic->stencil_reference.back = info->ds.back.reference;
      }

      if (states & RADV_DYNAMIC_DEPTH_TEST_ENABLE) {
         dynamic->depth_test_enable = info->ds.depth_test_enable;
      }

      if (states & RADV_DYNAMIC_DEPTH_WRITE_ENABLE) {
         dynamic->depth_write_enable = info->ds.depth_write_enable;
      }

      if (states & RADV_DYNAMIC_DEPTH_COMPARE_OP) {
         dynamic->depth_compare_op = info->ds.depth_compare_op;
      }

      if (states & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) {
         dynamic->depth_bounds_test_enable = info->ds.depth_bounds_test_enable;
      }

      if (states & RADV_DYNAMIC_STENCIL_TEST_ENABLE) {
         dynamic->stencil_test_enable = info->ds.stencil_test_enable;
      }

      if (states & RADV_DYNAMIC_STENCIL_OP) {
         dynamic->stencil_op.front.compare_op = info->ds.front.compare_op;
         dynamic->stencil_op.front.fail_op = info->ds.front.fail_op;
         dynamic->stencil_op.front.pass_op = info->ds.front.pass_op;
         dynamic->stencil_op.front.depth_fail_op = info->ds.front.depth_fail_op;

         dynamic->stencil_op.back.compare_op = info->ds.back.compare_op;
         dynamic->stencil_op.back.fail_op = info->ds.back.fail_op;
         dynamic->stencil_op.back.pass_op = info->ds.back.pass_op;
         dynamic->stencil_op.back.depth_fail_op = info->ds.back.depth_fail_op;
      }
   }

   if (needed_states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
      dynamic->discard_rectangle.count = info->dr.count;
      if (states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
         typed_memcpy(dynamic->discard_rectangle.rectangles, info->dr.rects, info->dr.count);
      }
   }

   if (needed_states & RADV_DYNAMIC_SAMPLE_LOCATIONS) {
      if (info->ms.sample_locs_enable) {
         dynamic->sample_location.per_pixel = info->ms.sample_locs_per_pixel;
         dynamic->sample_location.grid_size = info->ms.sample_locs_grid_size;
         dynamic->sample_location.count = info->ms.sample_locs_count;
         typed_memcpy(&dynamic->sample_location.locations[0], info->ms.sample_locs,
                      info->ms.sample_locs_count);
      }
   }

   if (needed_states & RADV_DYNAMIC_LINE_STIPPLE) {
      dynamic->line_stipple.factor = info->rs.line_stipple_factor;
      dynamic->line_stipple.pattern = info->rs.line_stipple_pattern;
   }

   if (!(states & RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE) ||
       !(states & RADV_DYNAMIC_VERTEX_INPUT))
      pipeline->uses_dynamic_stride = true;

   if (states & RADV_DYNAMIC_FRAGMENT_SHADING_RATE) {
      dynamic->fragment_shading_rate.size = info->fsr.size;
      for (int i = 0; i < 2; i++)
         dynamic->fragment_shading_rate.combiner_ops[i] = info->fsr.combiner_ops[i];
   }

   if (states & RADV_DYNAMIC_DEPTH_BIAS_ENABLE) {
      dynamic->depth_bias_enable = info->rs.depth_bias_enable;
   }

   if (states & RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE) {
      dynamic->primitive_restart_enable = info->ia.primitive_restart_enable;
   }

   if (states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE) {
      dynamic->rasterizer_discard_enable = info->rs.discard_enable;
   }

   if (radv_pipeline_has_color_attachments(&info->ri) && states & RADV_DYNAMIC_LOGIC_OP) {
      if (info->cb.logic_op_enable) {
         dynamic->logic_op = info->cb.logic_op;
      } else {
         dynamic->logic_op = V_028808_ROP3_COPY;
      }
   }

   if (states & RADV_DYNAMIC_COLOR_WRITE_ENABLE) {
      dynamic->color_write_enable = info->cb.color_write_enable;
   }

   pipeline->dynamic_state.mask = states;
}

static void
radv_pipeline_init_raster_state(struct radv_graphics_pipeline *pipeline,
                                const struct radv_graphics_pipeline_info *info)
{
   const struct radv_device *device = pipeline->base.device;

   pipeline->pa_su_sc_mode_cntl =
      S_028814_FACE(info->rs.front_face) |
      S_028814_CULL_FRONT(!!(info->rs.cull_mode & VK_CULL_MODE_FRONT_BIT)) |
      S_028814_CULL_BACK(!!(info->rs.cull_mode & VK_CULL_MODE_BACK_BIT)) |
      S_028814_POLY_MODE(info->rs.polygon_mode != V_028814_X_DRAW_TRIANGLES) |
      S_028814_POLYMODE_FRONT_PTYPE(info->rs.polygon_mode) |
      S_028814_POLYMODE_BACK_PTYPE(info->rs.polygon_mode) |
      S_028814_POLY_OFFSET_FRONT_ENABLE(info->rs.depth_bias_enable) |
      S_028814_POLY_OFFSET_BACK_ENABLE(info->rs.depth_bias_enable) |
      S_028814_POLY_OFFSET_PARA_ENABLE(info->rs.depth_bias_enable) |
      S_028814_PROVOKING_VTX_LAST(info->rs.provoking_vtx_last);

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      /* It should also be set if PERPENDICULAR_ENDCAP_ENA is set. */
      pipeline->pa_su_sc_mode_cntl |=
         S_028814_KEEP_TOGETHER_ENABLE(info->rs.polygon_mode != V_028814_X_DRAW_TRIANGLES);
   }

   pipeline->pa_cl_clip_cntl =
      S_028810_DX_CLIP_SPACE_DEF(!pipeline->negative_one_to_one) |
      S_028810_ZCLIP_NEAR_DISABLE(info->rs.depth_clip_disable) |
      S_028810_ZCLIP_FAR_DISABLE(info->rs.depth_clip_disable) |
      S_028810_DX_RASTERIZATION_KILL(info->rs.discard_enable) |
      S_028810_DX_LINEAR_ATTR_CLIP_ENA(1);

   pipeline->uses_conservative_overestimate =
      info->rs.conservative_mode == VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;

   pipeline->depth_clamp_mode = RADV_DEPTH_CLAMP_MODE_VIEWPORT;
   if (!info->rs.depth_clamp_enable) {
      /* For optimal performance, depth clamping should always be enabled except if the
       * application disables clamping explicitly or uses depth values outside of the [0.0, 1.0]
       * range.
       */
      if (info->rs.depth_clip_disable ||
          device->vk.enabled_extensions.EXT_depth_range_unrestricted) {
         pipeline->depth_clamp_mode = RADV_DEPTH_CLAMP_MODE_DISABLED;
      } else {
         pipeline->depth_clamp_mode = RADV_DEPTH_CLAMP_MODE_ZERO_TO_ONE;
      }
   }
}

static struct radv_depth_stencil_state
radv_pipeline_init_depth_stencil_state(struct radv_graphics_pipeline *pipeline,
                                       const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_depth_stencil_state ds_state = {0};
   uint32_t db_depth_control = 0;

   bool has_depth_attachment = info->ri.depth_att_format != VK_FORMAT_UNDEFINED;
   bool has_stencil_attachment = info->ri.stencil_att_format != VK_FORMAT_UNDEFINED;

   if (has_depth_attachment) {
      /* from amdvlk: For 4xAA and 8xAA need to decompress on flush for better performance */
      ds_state.db_render_override2 |= S_028010_DECOMPRESS_Z_ON_FLUSH(info->ms.raster_samples > 2);

      if (pdevice->rad_info.gfx_level >= GFX10_3)
         ds_state.db_render_override2 |= S_028010_CENTROID_COMPUTATION_MODE(1);

      db_depth_control = S_028800_Z_ENABLE(info->ds.depth_test_enable) |
                         S_028800_Z_WRITE_ENABLE(info->ds.depth_write_enable) |
                         S_028800_ZFUNC(info->ds.depth_compare_op) |
                         S_028800_DEPTH_BOUNDS_ENABLE(info->ds.depth_bounds_test_enable);
   }

   if (has_stencil_attachment && info->ds.stencil_test_enable) {
      db_depth_control |= S_028800_STENCIL_ENABLE(1) | S_028800_BACKFACE_ENABLE(1);
      db_depth_control |= S_028800_STENCILFUNC(info->ds.front.compare_op);
      db_depth_control |= S_028800_STENCILFUNC_BF(info->ds.back.compare_op);
   }

   ds_state.db_render_override |= S_02800C_FORCE_HIS_ENABLE0(V_02800C_FORCE_DISABLE) |
                                  S_02800C_FORCE_HIS_ENABLE1(V_02800C_FORCE_DISABLE);

   if (pipeline->depth_clamp_mode == RADV_DEPTH_CLAMP_MODE_DISABLED)
      ds_state.db_render_override |= S_02800C_DISABLE_VIEWPORT_CLAMP(1);

   if (pdevice->rad_info.gfx_level >= GFX11) {
      unsigned max_allowed_tiles_in_wave = 0;
      unsigned num_samples = MAX2(radv_pipeline_color_samples(info),
                                  radv_pipeline_depth_samples(info));

      if (pdevice->rad_info.has_dedicated_vram) {
         if (num_samples == 8)
            max_allowed_tiles_in_wave = 7;
         else if (num_samples == 4)
            max_allowed_tiles_in_wave = 14;
      } else {
         if (num_samples == 8)
            max_allowed_tiles_in_wave = 8;
      }

      /* TODO: We may want to disable this workaround for future chips. */
      if (num_samples >= 4) {
         if (max_allowed_tiles_in_wave)
            max_allowed_tiles_in_wave--;
         else
            max_allowed_tiles_in_wave = 15;
      }

      ds_state.db_render_control |= S_028000_OREO_MODE(V_028000_OMODE_O_THEN_B) |
                                    S_028000_MAX_ALLOWED_TILES_IN_WAVE(max_allowed_tiles_in_wave);
   }

   pipeline->db_depth_control = db_depth_control;

   return ds_state;
}

static void
gfx9_get_gs_info(const struct radv_pipeline_key *key, const struct radv_pipeline *pipeline,
                 struct radv_pipeline_stage *stages, struct gfx9_gs_info *out)
{
   const struct radv_physical_device *pdevice = pipeline->device->physical_device;
   struct radv_shader_info *gs_info = &stages[MESA_SHADER_GEOMETRY].info;
   struct radv_es_output_info *es_info;
   bool has_tess = !!stages[MESA_SHADER_TESS_CTRL].nir;

   if (pdevice->rad_info.gfx_level >= GFX9)
      es_info = has_tess ? &gs_info->tes.es_info : &gs_info->vs.es_info;
   else
      es_info = has_tess ? &stages[MESA_SHADER_TESS_EVAL].info.tes.es_info
                         : &stages[MESA_SHADER_VERTEX].info.vs.es_info;

   unsigned gs_num_invocations = MAX2(gs_info->gs.invocations, 1);
   bool uses_adjacency;
   switch (key->vs.topology) {
   case V_008958_DI_PT_LINELIST_ADJ:
   case V_008958_DI_PT_LINESTRIP_ADJ:
   case V_008958_DI_PT_TRILIST_ADJ:
   case V_008958_DI_PT_TRISTRIP_ADJ:
      uses_adjacency = true;
      break;
   default:
      uses_adjacency = false;
      break;
   }

   /* All these are in dwords: */
   /* We can't allow using the whole LDS, because GS waves compete with
    * other shader stages for LDS space. */
   const unsigned max_lds_size = 8 * 1024;
   const unsigned esgs_itemsize = es_info->esgs_itemsize / 4;
   unsigned esgs_lds_size;

   /* All these are per subgroup: */
   const unsigned max_out_prims = 32 * 1024;
   const unsigned max_es_verts = 255;
   const unsigned ideal_gs_prims = 64;
   unsigned max_gs_prims, gs_prims;
   unsigned min_es_verts, es_verts, worst_case_es_verts;

   if (uses_adjacency || gs_num_invocations > 1)
      max_gs_prims = 127 / gs_num_invocations;
   else
      max_gs_prims = 255;

   /* MAX_PRIMS_PER_SUBGROUP = gs_prims * max_vert_out * gs_invocations.
    * Make sure we don't go over the maximum value.
    */
   if (gs_info->gs.vertices_out > 0) {
      max_gs_prims =
         MIN2(max_gs_prims, max_out_prims / (gs_info->gs.vertices_out * gs_num_invocations));
   }
   assert(max_gs_prims > 0);

   /* If the primitive has adjacency, halve the number of vertices
    * that will be reused in multiple primitives.
    */
   min_es_verts = gs_info->gs.vertices_in / (uses_adjacency ? 2 : 1);

   gs_prims = MIN2(ideal_gs_prims, max_gs_prims);
   worst_case_es_verts = MIN2(min_es_verts * gs_prims, max_es_verts);

   /* Compute ESGS LDS size based on the worst case number of ES vertices
    * needed to create the target number of GS prims per subgroup.
    */
   esgs_lds_size = esgs_itemsize * worst_case_es_verts;

   /* If total LDS usage is too big, refactor partitions based on ratio
    * of ESGS item sizes.
    */
   if (esgs_lds_size > max_lds_size) {
      /* Our target GS Prims Per Subgroup was too large. Calculate
       * the maximum number of GS Prims Per Subgroup that will fit
       * into LDS, capped by the maximum that the hardware can support.
       */
      gs_prims = MIN2((max_lds_size / (esgs_itemsize * min_es_verts)), max_gs_prims);
      assert(gs_prims > 0);
      worst_case_es_verts = MIN2(min_es_verts * gs_prims, max_es_verts);

      esgs_lds_size = esgs_itemsize * worst_case_es_verts;
      assert(esgs_lds_size <= max_lds_size);
   }

   /* Now calculate remaining ESGS information. */
   if (esgs_lds_size)
      es_verts = MIN2(esgs_lds_size / esgs_itemsize, max_es_verts);
   else
      es_verts = max_es_verts;

   /* Vertices for adjacency primitives are not always reused, so restore
    * it for ES_VERTS_PER_SUBGRP.
    */
   min_es_verts = gs_info->gs.vertices_in;

   /* For normal primitives, the VGT only checks if they are past the ES
    * verts per subgroup after allocating a full GS primitive and if they
    * are, kick off a new subgroup.  But if those additional ES verts are
    * unique (e.g. not reused) we need to make sure there is enough LDS
    * space to account for those ES verts beyond ES_VERTS_PER_SUBGRP.
    */
   es_verts -= min_es_verts - 1;

   uint32_t es_verts_per_subgroup = es_verts;
   uint32_t gs_prims_per_subgroup = gs_prims;
   uint32_t gs_inst_prims_in_subgroup = gs_prims * gs_num_invocations;
   uint32_t max_prims_per_subgroup = gs_inst_prims_in_subgroup * gs_info->gs.vertices_out;
   out->lds_size = align(esgs_lds_size, 128) / 128;
   out->vgt_gs_onchip_cntl = S_028A44_ES_VERTS_PER_SUBGRP(es_verts_per_subgroup) |
                             S_028A44_GS_PRIMS_PER_SUBGRP(gs_prims_per_subgroup) |
                             S_028A44_GS_INST_PRIMS_IN_SUBGRP(gs_inst_prims_in_subgroup);
   out->vgt_gs_max_prims_per_subgroup = S_028A94_MAX_PRIMS_PER_SUBGROUP(max_prims_per_subgroup);
   out->vgt_esgs_ring_itemsize = esgs_itemsize;
   assert(max_prims_per_subgroup <= max_out_prims);

   gl_shader_stage es_stage = has_tess ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
   unsigned workgroup_size = ac_compute_esgs_workgroup_size(
      pdevice->rad_info.gfx_level, stages[es_stage].info.wave_size,
      es_verts_per_subgroup, gs_inst_prims_in_subgroup);
   stages[es_stage].info.workgroup_size = workgroup_size;
   stages[MESA_SHADER_GEOMETRY].info.workgroup_size = workgroup_size;
}

static void
clamp_gsprims_to_esverts(unsigned *max_gsprims, unsigned max_esverts, unsigned min_verts_per_prim,
                         bool use_adjacency)
{
   unsigned max_reuse = max_esverts - min_verts_per_prim;
   if (use_adjacency)
      max_reuse /= 2;
   *max_gsprims = MIN2(*max_gsprims, 1 + max_reuse);
}

static unsigned
radv_get_num_input_vertices(const struct radv_pipeline_stage *stages)
{
   if (stages[MESA_SHADER_GEOMETRY].nir) {
      nir_shader *gs = stages[MESA_SHADER_GEOMETRY].nir;

      return gs->info.gs.vertices_in;
   }

   if (stages[MESA_SHADER_TESS_CTRL].nir) {
      nir_shader *tes = stages[MESA_SHADER_TESS_EVAL].nir;

      if (tes->info.tess.point_mode)
         return 1;
      if (tes->info.tess._primitive_mode == TESS_PRIMITIVE_ISOLINES)
         return 2;
      return 3;
   }

   return 3;
}

static void
gfx10_emit_ge_pc_alloc(struct radeon_cmdbuf *cs, enum amd_gfx_level gfx_level,
                       uint32_t oversub_pc_lines)
{
   radeon_set_uconfig_reg(
      cs, R_030980_GE_PC_ALLOC,
      S_030980_OVERSUB_EN(oversub_pc_lines > 0) | S_030980_NUM_PC_LINES(oversub_pc_lines - 1));
}

static void
gfx10_get_ngg_ms_info(struct radv_pipeline_stage *stage, struct gfx10_ngg_info *ngg)
{
   /* Special case for mesh shader workgroups.
    *
    * Mesh shaders don't have any real vertex input, but they can produce
    * an arbitrary number of vertices and primitives (up to 256).
    * We need to precisely control the number of mesh shader workgroups
    * that are launched from draw calls.
    *
    * To achieve that, we set:
    * - input primitive topology to point list
    * - input vertex and primitive count to 1
    * - max output vertex count and primitive amplification factor
    *   to the boundaries of the shader
    *
    * With that, in the draw call:
    * - drawing 1 input vertex ~ launching 1 mesh shader workgroup
    *
    * In the shader:
    * - base vertex ~ first workgroup index (firstTask in NV_mesh_shader)
    * - input vertex id ~ workgroup id (in 1D - shader needs to calculate in 3D)
    *
    * Notes:
    * - without GS_EN=1 PRIM_AMP_FACTOR and MAX_VERTS_PER_SUBGROUP don't seem to work
    * - with GS_EN=1 we must also set VGT_GS_MAX_VERT_OUT (otherwise the GPU hangs)
    * - with GS_FAST_LAUNCH=1 every lane's VGPRs are initialized to the same input vertex index
    *
    */
   nir_shader *ms = stage->nir;

   ngg->enable_vertex_grouping = true;
   ngg->esgs_ring_size = 1;
   ngg->hw_max_esverts = 1;
   ngg->max_gsprims = 1;
   ngg->max_out_verts = ms->info.mesh.max_vertices_out;
   ngg->max_vert_out_per_gs_instance = false;
   ngg->ngg_emit_size = 0;
   ngg->prim_amp_factor = ms->info.mesh.max_primitives_out;
   ngg->vgt_esgs_ring_itemsize = 1;

   unsigned min_ngg_workgroup_size =
      ac_compute_ngg_workgroup_size(ngg->hw_max_esverts, ngg->max_gsprims,
                                    ngg->max_out_verts, ngg->prim_amp_factor);

   unsigned api_workgroup_size =
      ac_compute_cs_workgroup_size(ms->info.workgroup_size, false, UINT32_MAX);

   stage->info.workgroup_size = MAX2(min_ngg_workgroup_size, api_workgroup_size);
}

static void
gfx10_get_ngg_info(const struct radv_pipeline_key *key, struct radv_pipeline *pipeline,
                   struct radv_pipeline_stage *stages, struct gfx10_ngg_info *ngg)
{
   const struct radv_physical_device *pdevice = pipeline->device->physical_device;
   struct radv_shader_info *gs_info = &stages[MESA_SHADER_GEOMETRY].info;
   struct radv_es_output_info *es_info =
      stages[MESA_SHADER_TESS_CTRL].nir ? &gs_info->tes.es_info : &gs_info->vs.es_info;
   unsigned gs_type = stages[MESA_SHADER_GEOMETRY].nir ? MESA_SHADER_GEOMETRY : MESA_SHADER_VERTEX;
   unsigned max_verts_per_prim = radv_get_num_input_vertices(stages);
   unsigned min_verts_per_prim = gs_type == MESA_SHADER_GEOMETRY ? max_verts_per_prim : 1;
   unsigned gs_num_invocations = stages[MESA_SHADER_GEOMETRY].nir ? MAX2(gs_info->gs.invocations, 1) : 1;
   bool uses_adjacency;
   switch (key->vs.topology) {
   case V_008958_DI_PT_LINELIST_ADJ:
   case V_008958_DI_PT_LINESTRIP_ADJ:
   case V_008958_DI_PT_TRILIST_ADJ:
   case V_008958_DI_PT_TRISTRIP_ADJ:
      uses_adjacency = true;
      break;
   default:
      uses_adjacency = false;
      break;
   }

   /* All these are in dwords: */
   /* We can't allow using the whole LDS, because GS waves compete with
    * other shader stages for LDS space.
    *
    * TODO: We should really take the shader's internal LDS use into
    *       account. The linker will fail if the size is greater than
    *       8K dwords.
    */
   const unsigned max_lds_size = 8 * 1024 - 768;
   const unsigned target_lds_size = max_lds_size;
   unsigned esvert_lds_size = 0;
   unsigned gsprim_lds_size = 0;

   /* All these are per subgroup: */
   const unsigned min_esverts = pdevice->rad_info.gfx_level >= GFX10_3 ? 29 : 24;
   bool max_vert_out_per_gs_instance = false;
   unsigned max_esverts_base = 128;
   unsigned max_gsprims_base = 128; /* default prim group size clamp */

   /* Hardware has the following non-natural restrictions on the value
    * of GE_CNTL.VERT_GRP_SIZE based on based on the primitive type of
    * the draw:
    *  - at most 252 for any line input primitive type
    *  - at most 251 for any quad input primitive type
    *  - at most 251 for triangle strips with adjacency (this happens to
    *    be the natural limit for triangle *lists* with adjacency)
    */
   max_esverts_base = MIN2(max_esverts_base, 251 + max_verts_per_prim - 1);

   if (gs_type == MESA_SHADER_GEOMETRY) {
      unsigned max_out_verts_per_gsprim = gs_info->gs.vertices_out * gs_num_invocations;

      if (max_out_verts_per_gsprim <= 256) {
         if (max_out_verts_per_gsprim) {
            max_gsprims_base = MIN2(max_gsprims_base, 256 / max_out_verts_per_gsprim);
         }
      } else {
         /* Use special multi-cycling mode in which each GS
          * instance gets its own subgroup. Does not work with
          * tessellation. */
         max_vert_out_per_gs_instance = true;
         max_gsprims_base = 1;
         max_out_verts_per_gsprim = gs_info->gs.vertices_out;
      }

      esvert_lds_size = es_info->esgs_itemsize / 4;
      gsprim_lds_size = (gs_info->gs.gsvs_vertex_size / 4 + 1) * max_out_verts_per_gsprim;
   } else {
      /* VS and TES. */
      /* LDS size for passing data from GS to ES. */
      struct radv_streamout_info *so_info = stages[MESA_SHADER_TESS_CTRL].nir
                                               ? &stages[MESA_SHADER_TESS_EVAL].info.so
                                               : &stages[MESA_SHADER_VERTEX].info.so;

      if (so_info->num_outputs)
         esvert_lds_size = 4 * so_info->num_outputs + 1;

      /* GS stores Primitive IDs (one DWORD) into LDS at the address
       * corresponding to the ES thread of the provoking vertex. All
       * ES threads load and export PrimitiveID for their thread.
       */
      if (!stages[MESA_SHADER_TESS_CTRL].nir && stages[MESA_SHADER_VERTEX].info.vs.outinfo.export_prim_id)
         esvert_lds_size = MAX2(esvert_lds_size, 1);
   }

   unsigned max_gsprims = max_gsprims_base;
   unsigned max_esverts = max_esverts_base;

   if (esvert_lds_size)
      max_esverts = MIN2(max_esverts, target_lds_size / esvert_lds_size);
   if (gsprim_lds_size)
      max_gsprims = MIN2(max_gsprims, target_lds_size / gsprim_lds_size);

   max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
   clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, uses_adjacency);
   assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);

   if (esvert_lds_size || gsprim_lds_size) {
      /* Now that we have a rough proportionality between esverts
       * and gsprims based on the primitive type, scale both of them
       * down simultaneously based on required LDS space.
       *
       * We could be smarter about this if we knew how much vertex
       * reuse to expect.
       */
      unsigned lds_total = max_esverts * esvert_lds_size + max_gsprims * gsprim_lds_size;
      if (lds_total > target_lds_size) {
         max_esverts = max_esverts * target_lds_size / lds_total;
         max_gsprims = max_gsprims * target_lds_size / lds_total;

         max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
         clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, uses_adjacency);
         assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);
      }
   }

   /* Round up towards full wave sizes for better ALU utilization. */
   if (!max_vert_out_per_gs_instance) {
      unsigned orig_max_esverts;
      unsigned orig_max_gsprims;
      unsigned wavesize;

      if (gs_type == MESA_SHADER_GEOMETRY) {
         wavesize = gs_info->wave_size;
      } else {
         wavesize = stages[MESA_SHADER_TESS_CTRL].nir ? stages[MESA_SHADER_TESS_EVAL].info.wave_size
                                                      : stages[MESA_SHADER_VERTEX].info.wave_size;
      }

      do {
         orig_max_esverts = max_esverts;
         orig_max_gsprims = max_gsprims;

         max_esverts = align(max_esverts, wavesize);
         max_esverts = MIN2(max_esverts, max_esverts_base);
         if (esvert_lds_size)
            max_esverts =
               MIN2(max_esverts, (max_lds_size - max_gsprims * gsprim_lds_size) / esvert_lds_size);
         max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);

         /* Hardware restriction: minimum value of max_esverts */
         if (pdevice->rad_info.gfx_level == GFX10)
            max_esverts = MAX2(max_esverts, min_esverts - 1 + max_verts_per_prim);
         else
            max_esverts = MAX2(max_esverts, min_esverts);

         max_gsprims = align(max_gsprims, wavesize);
         max_gsprims = MIN2(max_gsprims, max_gsprims_base);
         if (gsprim_lds_size) {
            /* Don't count unusable vertices to the LDS
             * size. Those are vertices above the maximum
             * number of vertices that can occur in the
             * workgroup, which is e.g. max_gsprims * 3
             * for triangles.
             */
            unsigned usable_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
            max_gsprims = MIN2(max_gsprims,
                               (max_lds_size - usable_esverts * esvert_lds_size) / gsprim_lds_size);
         }
         clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, uses_adjacency);
         assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);
      } while (orig_max_esverts != max_esverts || orig_max_gsprims != max_gsprims);

      /* Verify the restriction. */
      if (pdevice->rad_info.gfx_level == GFX10)
         assert(max_esverts >= min_esverts - 1 + max_verts_per_prim);
      else
         assert(max_esverts >= min_esverts);
   } else {
      /* Hardware restriction: minimum value of max_esverts */
      if (pdevice->rad_info.gfx_level == GFX10)
         max_esverts = MAX2(max_esverts, min_esverts - 1 + max_verts_per_prim);
      else
         max_esverts = MAX2(max_esverts, min_esverts);
   }

   unsigned max_out_vertices = max_vert_out_per_gs_instance ? gs_info->gs.vertices_out
                               : gs_type == MESA_SHADER_GEOMETRY
                                  ? max_gsprims * gs_num_invocations * gs_info->gs.vertices_out
                                  : max_esverts;
   assert(max_out_vertices <= 256);

   unsigned prim_amp_factor = 1;
   if (gs_type == MESA_SHADER_GEOMETRY) {
      /* Number of output primitives per GS input primitive after
       * GS instancing. */
      prim_amp_factor = gs_info->gs.vertices_out;
   }

   /* On Gfx10, the GE only checks against the maximum number of ES verts
    * after allocating a full GS primitive. So we need to ensure that
    * whenever this check passes, there is enough space for a full
    * primitive without vertex reuse.
    */
   if (pdevice->rad_info.gfx_level == GFX10)
      ngg->hw_max_esverts = max_esverts - max_verts_per_prim + 1;
   else
      ngg->hw_max_esverts = max_esverts;

   ngg->max_gsprims = max_gsprims;
   ngg->max_out_verts = max_out_vertices;
   ngg->prim_amp_factor = prim_amp_factor;
   ngg->max_vert_out_per_gs_instance = max_vert_out_per_gs_instance;
   ngg->ngg_emit_size = max_gsprims * gsprim_lds_size;
   ngg->enable_vertex_grouping = true;

   /* Don't count unusable vertices. */
   ngg->esgs_ring_size = MIN2(max_esverts, max_gsprims * max_verts_per_prim) * esvert_lds_size * 4;

   if (gs_type == MESA_SHADER_GEOMETRY) {
      ngg->vgt_esgs_ring_itemsize = es_info->esgs_itemsize / 4;
   } else {
      ngg->vgt_esgs_ring_itemsize = 1;
   }

   assert(ngg->hw_max_esverts >= min_esverts); /* HW limitation */

   gl_shader_stage es_stage = stages[MESA_SHADER_TESS_CTRL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
   unsigned workgroup_size =
      ac_compute_ngg_workgroup_size(
         max_esverts, max_gsprims * gs_num_invocations, max_out_vertices, prim_amp_factor);
   stages[MESA_SHADER_GEOMETRY].info.workgroup_size = workgroup_size;
   stages[es_stage].info.workgroup_size = workgroup_size;
}

static void
radv_pipeline_init_gs_ring_state(struct radv_graphics_pipeline *pipeline, const struct gfx9_gs_info *gs)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   unsigned num_se = pdevice->rad_info.max_se;
   unsigned wave_size = 64;
   unsigned max_gs_waves = 32 * num_se; /* max 32 per SE on GCN */
   /* On GFX6-GFX7, the value comes from VGT_GS_VERTEX_REUSE = 16.
    * On GFX8+, the value comes from VGT_VERTEX_REUSE_BLOCK_CNTL = 30 (+2).
    */
   unsigned gs_vertex_reuse = (pdevice->rad_info.gfx_level >= GFX8 ? 32 : 16) * num_se;
   unsigned alignment = 256 * num_se;
   /* The maximum size is 63.999 MB per SE. */
   unsigned max_size = ((unsigned)(63.999 * 1024 * 1024) & ~255) * num_se;
   struct radv_shader_info *gs_info = &pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info;

   /* Calculate the minimum size. */
   unsigned min_esgs_ring_size =
      align(gs->vgt_esgs_ring_itemsize * 4 * gs_vertex_reuse * wave_size, alignment);
   /* These are recommended sizes, not minimum sizes. */
   unsigned esgs_ring_size =
      max_gs_waves * 2 * wave_size * gs->vgt_esgs_ring_itemsize * 4 * gs_info->gs.vertices_in;
   unsigned gsvs_ring_size = max_gs_waves * 2 * wave_size * gs_info->gs.max_gsvs_emit_size;

   min_esgs_ring_size = align(min_esgs_ring_size, alignment);
   esgs_ring_size = align(esgs_ring_size, alignment);
   gsvs_ring_size = align(gsvs_ring_size, alignment);

   if (pdevice->rad_info.gfx_level <= GFX8)
      pipeline->esgs_ring_size = CLAMP(esgs_ring_size, min_esgs_ring_size, max_size);

   pipeline->gsvs_ring_size = MIN2(gsvs_ring_size, max_size);
}

struct radv_shader *
radv_get_shader(const struct radv_pipeline *pipeline, gl_shader_stage stage)
{
   if (stage == MESA_SHADER_VERTEX) {
      if (pipeline->shaders[MESA_SHADER_VERTEX])
         return pipeline->shaders[MESA_SHADER_VERTEX];
      if (pipeline->shaders[MESA_SHADER_TESS_CTRL])
         return pipeline->shaders[MESA_SHADER_TESS_CTRL];
      if (pipeline->shaders[MESA_SHADER_GEOMETRY])
         return pipeline->shaders[MESA_SHADER_GEOMETRY];
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      if (!pipeline->shaders[MESA_SHADER_TESS_CTRL])
         return NULL;
      if (pipeline->shaders[MESA_SHADER_TESS_EVAL])
         return pipeline->shaders[MESA_SHADER_TESS_EVAL];
      if (pipeline->shaders[MESA_SHADER_GEOMETRY])
         return pipeline->shaders[MESA_SHADER_GEOMETRY];
   }
   return pipeline->shaders[stage];
}

static const struct radv_vs_output_info *
get_vs_output_info(const struct radv_graphics_pipeline *pipeline)
{
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
      if (radv_pipeline_has_ngg(pipeline))
         return &pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.vs.outinfo;
      else
         return &pipeline->base.gs_copy_shader->info.vs.outinfo;
   else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL))
      return &pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.tes.outinfo;
   else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      return &pipeline->base.shaders[MESA_SHADER_MESH]->info.ms.outinfo;
   else
      return &pipeline->base.shaders[MESA_SHADER_VERTEX]->info.vs.outinfo;
}

static bool
radv_lower_viewport_to_zero(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* There should be only one deref load for VIEWPORT after lower_io_to_temporaries. */
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_load_deref)
            continue;

         nir_variable *var = nir_intrinsic_get_var(intr, 0);
         if (var->data.mode != nir_var_shader_in ||
             var->data.location != VARYING_SLOT_VIEWPORT)
            continue;

         b.cursor = nir_before_instr(instr);

         nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_imm_zero(&b, 1, 32));
         progress = true;
         break;
      }
      if (progress)
         break;
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static nir_variable *
find_layer_out_var(nir_shader *nir)
{
   nir_variable *var = nir_find_variable_with_location(nir, nir_var_shader_out, VARYING_SLOT_LAYER);
   if (var != NULL)
      return var;

   var = nir_variable_create(nir, nir_var_shader_out, glsl_int_type(), "layer id");
   var->data.location = VARYING_SLOT_LAYER;
   var->data.interpolation = INTERP_MODE_NONE;

   return var;
}

static bool
radv_lower_multiview(nir_shader *nir)
{
   /* This pass is not suitable for mesh shaders, because it can't know
    * the mapping between API mesh shader invocations and output primitives.
    * Needs to be handled in ac_nir_lower_ngg.
    */
   if (nir->info.stage == MESA_SHADER_MESH)
      return false;

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* Iterate in reverse order since there should be only one deref store to POS after
    * lower_io_to_temporaries for vertex shaders and inject the layer there. For geometry shaders,
    * the layer is injected right before every emit_vertex_with_counter.
    */
   nir_variable *layer = NULL;
   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         if (nir->info.stage == MESA_SHADER_GEOMETRY) {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_emit_vertex_with_counter)
               continue;

            b.cursor = nir_before_instr(instr);
         } else {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_variable *var = nir_intrinsic_get_var(intr, 0);
            if (var->data.mode != nir_var_shader_out || var->data.location != VARYING_SLOT_POS)
               continue;

            b.cursor = nir_after_instr(instr);
         }

         if (!layer)
            layer = find_layer_out_var(nir);

         nir_store_var(&b, layer, nir_load_view_index(&b), 1);

         /* Update outputs_written to reflect that the pass added a new output. */
         nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_LAYER);

         progress = true;
         if (nir->info.stage == MESA_SHADER_VERTEX)
            break;
      }
      if (nir->info.stage == MESA_SHADER_VERTEX && progress)
         break;
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static bool
radv_export_implicit_primitive_id(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b;
   nir_builder_init(&b, impl);

   b.cursor = nir_after_cf_list(&impl->body);

   nir_variable *var = nir_variable_create(nir, nir_var_shader_out, glsl_int_type(), NULL);
   var->data.location = VARYING_SLOT_PRIMITIVE_ID;
   var->data.interpolation = INTERP_MODE_NONE;

   nir_store_var(&b, var, nir_load_primitive_id(&b), 1);

   /* Update outputs_written to reflect that the pass added a new output. */
   nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_ID);

   nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);

   return true;
}

static void
radv_link_shaders(struct radv_pipeline *pipeline,
                  const struct radv_pipeline_key *pipeline_key,
                  const struct radv_pipeline_stage *stages,
                  bool optimize_conservatively,
                  gl_shader_stage last_vgt_api_stage)
{
   const struct radv_physical_device *pdevice = pipeline->device->physical_device;
   nir_shader *ordered_shaders[MESA_VULKAN_SHADER_STAGES];
   int shader_count = 0;

   if (stages[MESA_SHADER_FRAGMENT].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_FRAGMENT].nir;
   }
   if (stages[MESA_SHADER_GEOMETRY].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_GEOMETRY].nir;
   }
   if (stages[MESA_SHADER_TESS_EVAL].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_TESS_EVAL].nir;
   }
   if (stages[MESA_SHADER_TESS_CTRL].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_TESS_CTRL].nir;
   }
   if (stages[MESA_SHADER_VERTEX].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_VERTEX].nir;
   }
   if (stages[MESA_SHADER_MESH].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_MESH].nir;
   }
   if (stages[MESA_SHADER_TASK].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_TASK].nir;
   }
   if (stages[MESA_SHADER_COMPUTE].nir) {
      ordered_shaders[shader_count++] = stages[MESA_SHADER_COMPUTE].nir;
   }

   if (stages[MESA_SHADER_MESH].nir && stages[MESA_SHADER_FRAGMENT].nir) {
      nir_shader *ps = stages[MESA_SHADER_FRAGMENT].nir;

      nir_foreach_shader_in_variable(var, ps) {
         /* These variables are per-primitive when used with a mesh shader. */
         if (var->data.location == VARYING_SLOT_PRIMITIVE_ID ||
             var->data.location == VARYING_SLOT_VIEWPORT ||
             var->data.location == VARYING_SLOT_LAYER)
            var->data.per_primitive = true;
      }
   }

   bool has_geom_tess = stages[MESA_SHADER_GEOMETRY].nir || stages[MESA_SHADER_TESS_CTRL].nir;
   bool merged_gs = stages[MESA_SHADER_GEOMETRY].nir && pdevice->rad_info.gfx_level >= GFX9;

   if (!optimize_conservatively && shader_count > 1) {
      unsigned first = ordered_shaders[shader_count - 1]->info.stage;
      unsigned last = ordered_shaders[0]->info.stage;

      if (ordered_shaders[0]->info.stage == MESA_SHADER_FRAGMENT &&
          ordered_shaders[1]->info.has_transform_feedback_varyings)
         nir_link_xfb_varyings(ordered_shaders[1], ordered_shaders[0]);

      for (int i = 1; i < shader_count; ++i) {
         nir_lower_io_arrays_to_elements(ordered_shaders[i], ordered_shaders[i - 1]);
         nir_validate_shader(ordered_shaders[i], "after nir_lower_io_arrays_to_elements");
         nir_validate_shader(ordered_shaders[i - 1], "after nir_lower_io_arrays_to_elements");
      }

      for (int i = 0; i < shader_count; ++i) {
         nir_variable_mode mask = 0;

         if (ordered_shaders[i]->info.stage != first)
            mask = mask | nir_var_shader_in;

         if (ordered_shaders[i]->info.stage != last)
            mask = mask | nir_var_shader_out;

         bool progress = false;
         NIR_PASS(progress, ordered_shaders[i], nir_lower_io_to_scalar_early, mask);
         if (progress) {
            /* Optimize the new vector code and then remove dead vars */
            NIR_PASS(_, ordered_shaders[i], nir_copy_prop);
            NIR_PASS(_, ordered_shaders[i], nir_opt_shrink_vectors);

            if (ordered_shaders[i]->info.stage != last) {
               /* Optimize swizzled movs of load_const for
                * nir_link_opt_varyings's constant propagation
                */
               NIR_PASS(_, ordered_shaders[i], nir_opt_constant_folding);
               /* For nir_link_opt_varyings's duplicate input opt */
               NIR_PASS(_, ordered_shaders[i], nir_opt_cse);
            }

            /* Run copy-propagation to help remove dead
             * output variables (some shaders have useless
             * copies to/from an output), so compaction
             * later will be more effective.
             *
             * This will have been done earlier but it might
             * not have worked because the outputs were vector.
             */
            if (ordered_shaders[i]->info.stage == MESA_SHADER_TESS_CTRL)
               NIR_PASS(_, ordered_shaders[i], nir_opt_copy_prop_vars);

            NIR_PASS(_, ordered_shaders[i], nir_opt_dce);
            NIR_PASS(_, ordered_shaders[i], nir_remove_dead_variables,
                     nir_var_function_temp | nir_var_shader_in | nir_var_shader_out, NULL);
         }
      }
   }

   /* Export the primitive ID when VS or TES don't export it because it's implicit, while it's
    * required for GS or MS. The primitive ID is added during lowering for NGG.
    */
   if (stages[MESA_SHADER_FRAGMENT].nir &&
       (stages[MESA_SHADER_FRAGMENT].nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID) &&
       !(stages[last_vgt_api_stage].nir->info.outputs_written & VARYING_BIT_PRIMITIVE_ID) &&
       ((last_vgt_api_stage == MESA_SHADER_VERTEX && !stages[MESA_SHADER_VERTEX].info.is_ngg) ||
        (last_vgt_api_stage == MESA_SHADER_TESS_EVAL && !stages[MESA_SHADER_TESS_EVAL].info.is_ngg))) {
      radv_export_implicit_primitive_id(stages[last_vgt_api_stage].nir);
   }

   if (!optimize_conservatively) {
      bool uses_xfb = last_vgt_api_stage != -1 &&
                      stages[last_vgt_api_stage].nir->xfb_info;

      for (unsigned i = 0; i < shader_count; ++i) {
         shader_info *info = &ordered_shaders[i]->info;

         /* Remove exports without color attachment or writemask. */
         if (info->stage == MESA_SHADER_FRAGMENT) {
            bool fixup_derefs = false;
            nir_foreach_variable_with_modes(var, ordered_shaders[i], nir_var_shader_out) {
               int idx = var->data.location;
               idx -= FRAG_RESULT_DATA0;
               if (idx < 0)
                  continue;

               unsigned col_format = (pipeline_key->ps.col_format >> (4 * idx)) & 0xf;
               unsigned cb_target_mask = (pipeline_key->ps.cb_target_mask >> (4 * idx)) & 0xf;

               if (col_format == V_028714_SPI_SHADER_ZERO ||
                   (col_format == V_028714_SPI_SHADER_32_R && !cb_target_mask &&
                    !pipeline_key->ps.mrt0_is_dual_src)) {
                  /* Remove the color export if it's unused or in presence of holes. */
                  info->outputs_written &= ~BITFIELD64_BIT(var->data.location);
                  var->data.location = 0;
                  var->data.mode = nir_var_shader_temp;
                  fixup_derefs = true;
               }
            }
            if (fixup_derefs) {
               NIR_PASS_V(ordered_shaders[i], nir_fixup_deref_modes);
               NIR_PASS(_, ordered_shaders[i], nir_remove_dead_variables, nir_var_shader_temp,
                        NULL);
               NIR_PASS(_, ordered_shaders[i], nir_opt_dce);
            }
            continue;
         }

         /* Remove PSIZ from shaders when it's not needed.
          * This is typically produced by translation layers like Zink or D9VK.
          */
         if (uses_xfb || !(info->outputs_written & VARYING_BIT_PSIZ))
            continue;

         bool next_stage_needs_psiz =
            i != 0 && /* ordered_shaders is backwards, so next stage is: i - 1 */
            ordered_shaders[i - 1]->info.inputs_read & VARYING_BIT_PSIZ;
         bool topology_uses_psiz =
            info->stage == last_vgt_api_stage &&
            ((info->stage == MESA_SHADER_VERTEX && pipeline_key->vs.topology == V_008958_DI_PT_POINTLIST) ||
             (info->stage == MESA_SHADER_TESS_EVAL && info->tess.point_mode) ||
             (info->stage == MESA_SHADER_GEOMETRY && info->gs.output_primitive == SHADER_PRIM_POINTS) ||
             (info->stage == MESA_SHADER_MESH && info->mesh.primitive_type == SHADER_PRIM_POINTS));

         nir_variable *psiz_var =
               nir_find_variable_with_location(ordered_shaders[i], nir_var_shader_out, VARYING_SLOT_PSIZ);

         if (!next_stage_needs_psiz && !topology_uses_psiz && psiz_var) {
            /* Change PSIZ to a global variable which allows it to be DCE'd. */
            psiz_var->data.location = 0;
            psiz_var->data.mode = nir_var_shader_temp;

            info->outputs_written &= ~VARYING_BIT_PSIZ;
            NIR_PASS_V(ordered_shaders[i], nir_fixup_deref_modes);
            NIR_PASS(_, ordered_shaders[i], nir_remove_dead_variables, nir_var_shader_temp, NULL);
            NIR_PASS(_, ordered_shaders[i], nir_opt_dce);
         }
      }
   }

   /* Lower the viewport index to zero when the last vertex stage doesn't export it. */
   if (stages[MESA_SHADER_FRAGMENT].nir &&
       (stages[MESA_SHADER_FRAGMENT].nir->info.inputs_read & VARYING_BIT_VIEWPORT) &&
       !(stages[last_vgt_api_stage].nir->info.outputs_written & VARYING_BIT_VIEWPORT)) {
      NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, radv_lower_viewport_to_zero);
   }

   /* Export the layer in the last VGT stage if multiview is used. */
   if (pipeline_key->has_multiview_view_index && last_vgt_api_stage != -1 &&
       !(stages[last_vgt_api_stage].nir->info.outputs_written &
         VARYING_BIT_LAYER)) {
      nir_shader *last_vgt_shader = stages[last_vgt_api_stage].nir;
      NIR_PASS(_, last_vgt_shader, radv_lower_multiview);
   }

   for (int i = 1; !optimize_conservatively && (i < shader_count); ++i) {
      if (nir_link_opt_varyings(ordered_shaders[i], ordered_shaders[i - 1])) {
         nir_validate_shader(ordered_shaders[i], "after nir_link_opt_varyings");
         nir_validate_shader(ordered_shaders[i - 1], "after nir_link_opt_varyings");

         NIR_PASS(_, ordered_shaders[i - 1], nir_opt_constant_folding);
         NIR_PASS(_, ordered_shaders[i - 1], nir_opt_algebraic);
         NIR_PASS(_, ordered_shaders[i - 1], nir_opt_dce);
      }

      NIR_PASS(_, ordered_shaders[i], nir_remove_dead_variables, nir_var_shader_out, NULL);
      NIR_PASS(_, ordered_shaders[i - 1], nir_remove_dead_variables, nir_var_shader_in, NULL);

      bool progress = nir_remove_unused_varyings(ordered_shaders[i], ordered_shaders[i - 1]);

      nir_compact_varyings(ordered_shaders[i], ordered_shaders[i - 1], true);
      nir_validate_shader(ordered_shaders[i], "after nir_compact_varyings");
      nir_validate_shader(ordered_shaders[i - 1], "after nir_compact_varyings");
      if (ordered_shaders[i]->info.stage == MESA_SHADER_MESH) {
         /* nir_compact_varyings can change the location of per-vertex and per-primitive outputs */
         nir_shader_gather_info(ordered_shaders[i], nir_shader_get_entrypoint(ordered_shaders[i]));
      }

      if (ordered_shaders[i]->info.stage == MESA_SHADER_TESS_CTRL ||
          ordered_shaders[i]->info.stage == MESA_SHADER_MESH ||
          (ordered_shaders[i]->info.stage == MESA_SHADER_VERTEX && has_geom_tess) ||
          (ordered_shaders[i]->info.stage == MESA_SHADER_TESS_EVAL && merged_gs)) {
         NIR_PASS(_, ordered_shaders[i], nir_lower_io_to_vector, nir_var_shader_out);
         if (ordered_shaders[i]->info.stage == MESA_SHADER_TESS_CTRL)
            NIR_PASS(_, ordered_shaders[i], nir_vectorize_tess_levels);
         NIR_PASS(_, ordered_shaders[i], nir_opt_combine_stores, nir_var_shader_out);
      }
      if (ordered_shaders[i - 1]->info.stage == MESA_SHADER_GEOMETRY ||
          ordered_shaders[i - 1]->info.stage == MESA_SHADER_TESS_CTRL ||
          ordered_shaders[i - 1]->info.stage == MESA_SHADER_TESS_EVAL) {
         NIR_PASS(_, ordered_shaders[i - 1], nir_lower_io_to_vector, nir_var_shader_in);
      }

      if (progress) {
         progress = false;
         NIR_PASS(progress, ordered_shaders[i], nir_lower_global_vars_to_local);
         if (progress) {
            ac_nir_lower_indirect_derefs(ordered_shaders[i], pdevice->rad_info.gfx_level);
            /* remove dead writes, which can remove input loads */
            NIR_PASS(_, ordered_shaders[i], nir_lower_vars_to_ssa);
            NIR_PASS(_, ordered_shaders[i], nir_opt_dce);
         }

         progress = false;
         NIR_PASS(progress, ordered_shaders[i - 1], nir_lower_global_vars_to_local);
         if (progress) {
            ac_nir_lower_indirect_derefs(ordered_shaders[i - 1], pdevice->rad_info.gfx_level);
         }
      }
   }
}

static void
radv_set_driver_locations(struct radv_pipeline *pipeline, struct radv_pipeline_stage *stages,
                          gl_shader_stage last_vgt_api_stage)
{
   const struct radv_physical_device *pdevice = pipeline->device->physical_device;

   if (stages[MESA_SHADER_FRAGMENT].nir) {
      nir_foreach_shader_out_variable(var, stages[MESA_SHADER_FRAGMENT].nir)
      {
         var->data.driver_location = var->data.location + var->data.index;
      }
   }

   if (stages[MESA_SHADER_MESH].nir) {
      /* ac_nir_lower_ngg ignores driver locations for mesh shaders,
       * but set them to all zero just to be on the safe side.
       */
      nir_foreach_shader_out_variable(var, stages[MESA_SHADER_MESH].nir) {
         var->data.driver_location = 0;
      }
      return;
   }

   if (!stages[MESA_SHADER_VERTEX].nir)
      return;

   bool has_tess = stages[MESA_SHADER_TESS_CTRL].nir;
   bool has_gs = stages[MESA_SHADER_GEOMETRY].nir;

   /* Merged stage for VS and TES */
   unsigned vs_info_idx = MESA_SHADER_VERTEX;
   unsigned tes_info_idx = MESA_SHADER_TESS_EVAL;

   if (pdevice->rad_info.gfx_level >= GFX9) {
      /* These are merged into the next stage */
      vs_info_idx = has_tess ? MESA_SHADER_TESS_CTRL : MESA_SHADER_GEOMETRY;
      tes_info_idx = has_gs ? MESA_SHADER_GEOMETRY : MESA_SHADER_TESS_EVAL;
   }

   nir_foreach_shader_in_variable (var, stages[MESA_SHADER_VERTEX].nir) {
      var->data.driver_location = var->data.location;
   }

   if (has_tess) {
      nir_linked_io_var_info vs2tcs = nir_assign_linked_io_var_locations(
         stages[MESA_SHADER_VERTEX].nir, stages[MESA_SHADER_TESS_CTRL].nir);
      nir_linked_io_var_info tcs2tes = nir_assign_linked_io_var_locations(
         stages[MESA_SHADER_TESS_CTRL].nir, stages[MESA_SHADER_TESS_EVAL].nir);

      stages[MESA_SHADER_VERTEX].info.vs.num_linked_outputs = vs2tcs.num_linked_io_vars;
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_inputs = vs2tcs.num_linked_io_vars;
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_outputs = tcs2tes.num_linked_io_vars;
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_patch_outputs = tcs2tes.num_linked_patch_io_vars;
      stages[MESA_SHADER_TESS_EVAL].info.tes.num_linked_inputs = tcs2tes.num_linked_io_vars;
      stages[MESA_SHADER_TESS_EVAL].info.tes.num_linked_patch_inputs = tcs2tes.num_linked_patch_io_vars;

      /* Copy data to merged stage */
      stages[vs_info_idx].info.vs.num_linked_outputs = vs2tcs.num_linked_io_vars;
      stages[tes_info_idx].info.tes.num_linked_inputs = tcs2tes.num_linked_io_vars;
      stages[tes_info_idx].info.tes.num_linked_patch_inputs = tcs2tes.num_linked_patch_io_vars;

      if (has_gs) {
         nir_linked_io_var_info tes2gs = nir_assign_linked_io_var_locations(
            stages[MESA_SHADER_TESS_EVAL].nir, stages[MESA_SHADER_GEOMETRY].nir);

         stages[MESA_SHADER_TESS_EVAL].info.tes.num_linked_outputs = tes2gs.num_linked_io_vars;
         stages[MESA_SHADER_GEOMETRY].info.gs.num_linked_inputs = tes2gs.num_linked_io_vars;

         /* Copy data to merged stage */
         stages[tes_info_idx].info.tes.num_linked_outputs = tes2gs.num_linked_io_vars;
      }
   } else if (has_gs) {
      nir_linked_io_var_info vs2gs = nir_assign_linked_io_var_locations(
         stages[MESA_SHADER_VERTEX].nir, stages[MESA_SHADER_GEOMETRY].nir);

      stages[MESA_SHADER_VERTEX].info.vs.num_linked_outputs = vs2gs.num_linked_io_vars;
      stages[MESA_SHADER_GEOMETRY].info.gs.num_linked_inputs = vs2gs.num_linked_io_vars;

      /* Copy data to merged stage */
      stages[vs_info_idx].info.vs.num_linked_outputs = vs2gs.num_linked_io_vars;
   }

   assert(last_vgt_api_stage != MESA_SHADER_NONE);
   nir_foreach_shader_out_variable(var, stages[last_vgt_api_stage].nir)
   {
      var->data.driver_location = var->data.location;
   }
}

static struct radv_pipeline_key
radv_generate_pipeline_key(const struct radv_pipeline *pipeline, VkPipelineCreateFlags flags)
{
   struct radv_device *device = pipeline->device;
   struct radv_pipeline_key key;

   memset(&key, 0, sizeof(key));

   if (flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT)
      key.optimisations_disabled = 1;

   key.disable_aniso_single_level = device->instance->disable_aniso_single_level &&
                                    device->physical_device->rad_info.gfx_level < GFX8;

   key.image_2d_view_of_3d = device->image_2d_view_of_3d &&
                             device->physical_device->rad_info.gfx_level == GFX9;

   return key;
}

static struct radv_pipeline_key
radv_generate_graphics_pipeline_key(const struct radv_graphics_pipeline *pipeline,
                                    const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                    const struct radv_graphics_pipeline_info *info,
                                    const struct radv_blend_state *blend)
{
   struct radv_device *device = pipeline->base.device;
   struct radv_pipeline_key key = radv_generate_pipeline_key(&pipeline->base, pCreateInfo->flags);

   key.has_multiview_view_index = !!info->ri.view_mask;

   if (pipeline->dynamic_states & RADV_DYNAMIC_VERTEX_INPUT) {
      key.vs.dynamic_input_state = true;
   }

   /* Vertex input state */
   key.vs.instance_rate_inputs = info->vi.instance_rate_inputs;
   key.vs.vertex_post_shuffle = info->vi.vertex_post_shuffle;

   for (uint32_t i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
      key.vs.instance_rate_divisors[i] = info->vi.instance_rate_divisors[i];
      key.vs.vertex_attribute_formats[i] = info->vi.vertex_attribute_formats[i];
      key.vs.vertex_attribute_bindings[i] = info->vi.vertex_attribute_bindings[i];
      key.vs.vertex_attribute_offsets[i] = info->vi.vertex_attribute_offsets[i];
      key.vs.vertex_attribute_strides[i] = info->vi.vertex_attribute_strides[i];
      key.vs.vertex_alpha_adjust[i] = info->vi.vertex_alpha_adjust[i];
   }

   for (uint32_t i = 0; i < MAX_VBS; i++) {
      key.vs.vertex_binding_align[i] = info->vi.vertex_binding_align[i];
   }

   key.tcs.tess_input_vertices = info->ts.patch_control_points;

   if (info->ms.raster_samples > 1) {
      uint32_t ps_iter_samples = radv_pipeline_get_ps_iter_samples(info);
      key.ps.num_samples = info->ms.raster_samples;
      key.ps.log2_ps_iter_samples = util_logbase2(ps_iter_samples);
   }

   key.ps.col_format = blend->spi_shader_col_format;
   key.ps.cb_target_mask = blend->cb_target_mask;
   key.ps.mrt0_is_dual_src = blend->mrt0_is_dual_src;
   if (device->physical_device->rad_info.gfx_level < GFX8) {
      key.ps.is_int8 = blend->col_format_is_int8;
      key.ps.is_int10 = blend->col_format_is_int10;
   }
   if (device->physical_device->rad_info.gfx_level >= GFX11) {
      key.ps.alpha_to_coverage_via_mrtz = info->ms.alpha_to_coverage_enable;
   }

   key.vs.topology = info->ia.primitive_topology;

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      key.vs.provoking_vtx_last = info->rs.provoking_vtx_last;
   }

   if (device->instance->debug_flags & RADV_DEBUG_DISCARD_TO_DEMOTE)
      key.ps.lower_discard_to_demote = true;

   if (device->instance->enable_mrt_output_nan_fixup)
      key.ps.enable_mrt_output_nan_fixup = blend->col_format_is_float32;


   key.ps.force_vrs_enabled = device->force_vrs_enabled;

   if (device->instance->debug_flags & RADV_DEBUG_INVARIANT_GEOM)
      key.invariant_geom = true;

   key.use_ngg = device->physical_device->use_ngg;

   if ((radv_is_vrs_enabled(pipeline, info) || device->force_vrs_enabled) &&
       (device->physical_device->rad_info.family == CHIP_NAVI21 ||
        device->physical_device->rad_info.family == CHIP_NAVI22 ||
        device->physical_device->rad_info.family == CHIP_VANGOGH))
      key.adjust_frag_coord_z = true;

   if (device->instance->disable_sinking_load_input_fs)
      key.disable_sinking_load_input_fs = true;

   if (device->primitives_generated_query)
      key.primitives_generated_query = true;

   key.ps.has_epilog = false; /* TODO: hook up PS epilogs */

   return key;
}

static uint8_t
radv_get_wave_size(struct radv_device *device,  gl_shader_stage stage,
                   const struct radv_shader_info *info)
{
   if (stage == MESA_SHADER_GEOMETRY && !info->is_ngg)
      return 64;
   else if (stage == MESA_SHADER_COMPUTE) {
      return info->cs.subgroup_size;
   } else if (stage == MESA_SHADER_FRAGMENT)
      return device->physical_device->ps_wave_size;
   else if (stage == MESA_SHADER_TASK)
      return device->physical_device->cs_wave_size;
   else
      return device->physical_device->ge_wave_size;
}

static uint8_t
radv_get_ballot_bit_size(struct radv_device *device, gl_shader_stage stage,
                         const struct radv_shader_info *info)
{
   if (stage == MESA_SHADER_COMPUTE && info->cs.subgroup_size)
      return info->cs.subgroup_size;
   return 64;
}

static void
radv_determine_ngg_settings(struct radv_pipeline *pipeline,
                            const struct radv_pipeline_key *pipeline_key,
                            struct radv_pipeline_stage *stages,
                            gl_shader_stage last_vgt_api_stage)
{
   const struct radv_physical_device *pdevice = pipeline->device->physical_device;

   /* Shader settings for VS or TES without GS. */
   if (last_vgt_api_stage == MESA_SHADER_VERTEX ||
       last_vgt_api_stage == MESA_SHADER_TESS_EVAL) {
      uint64_t ps_inputs_read =
         stages[MESA_SHADER_FRAGMENT].nir ? stages[MESA_SHADER_FRAGMENT].nir->info.inputs_read : 0;
      gl_shader_stage es_stage = last_vgt_api_stage;

      unsigned num_vertices_per_prim = si_conv_prim_to_gs_out(pipeline_key->vs.topology) + 1;
      if (es_stage == MESA_SHADER_TESS_EVAL)
         num_vertices_per_prim = stages[es_stage].nir->info.tess.point_mode                      ? 1
                                 : stages[es_stage].nir->info.tess._primitive_mode == TESS_PRIMITIVE_ISOLINES ? 2
                                                                                          : 3;
      /* TODO: Enable culling for LLVM. */
      stages[es_stage].info.has_ngg_culling = radv_consider_culling(
         pdevice, stages[es_stage].nir, ps_inputs_read, num_vertices_per_prim, &stages[es_stage].info) &&
         !radv_use_llvm_for_stage(pipeline->device, es_stage);

      nir_function_impl *impl = nir_shader_get_entrypoint(stages[es_stage].nir);
      stages[es_stage].info.has_ngg_early_prim_export = exec_list_is_singular(&impl->body);

      /* Invocations that process an input vertex */
      const struct gfx10_ngg_info *ngg_info = &stages[es_stage].info.ngg_info;
      unsigned max_vtx_in = MIN2(256, ngg_info->enable_vertex_grouping ? ngg_info->hw_max_esverts : num_vertices_per_prim * ngg_info->max_gsprims);

      unsigned lds_bytes_if_culling_off = 0;
      /* We need LDS space when VS needs to export the primitive ID. */
      if (es_stage == MESA_SHADER_VERTEX && stages[es_stage].info.vs.outinfo.export_prim_id)
         lds_bytes_if_culling_off = max_vtx_in * 4u;
      stages[es_stage].info.num_lds_blocks_when_not_culling =
         DIV_ROUND_UP(lds_bytes_if_culling_off, pdevice->rad_info.lds_encode_granularity);

      /* NGG passthrough mode should be disabled when culling and when the vertex shader exports the
       * primitive ID.
       */
      stages[es_stage].info.is_ngg_passthrough = stages[es_stage].info.is_ngg_passthrough &&
                                                !stages[es_stage].info.has_ngg_culling &&
                                                 !(es_stage == MESA_SHADER_VERTEX &&
                                                   stages[es_stage].info.vs.outinfo.export_prim_id);
   }
}

static void
radv_fill_shader_info_ngg(struct radv_pipeline *pipeline,
                          const struct radv_pipeline_key *pipeline_key,
                          struct radv_pipeline_stage *stages)
{
   struct radv_device *device = pipeline->device;

   if (pipeline_key->use_ngg) {
      if (stages[MESA_SHADER_TESS_CTRL].nir) {
         stages[MESA_SHADER_TESS_EVAL].info.is_ngg = true;
      } else if (stages[MESA_SHADER_VERTEX].nir) {
         stages[MESA_SHADER_VERTEX].info.is_ngg = true;
      } else if (stages[MESA_SHADER_MESH].nir) {
         stages[MESA_SHADER_MESH].info.is_ngg = true;
      }

      if (stages[MESA_SHADER_TESS_CTRL].nir && stages[MESA_SHADER_GEOMETRY].nir &&
          stages[MESA_SHADER_GEOMETRY].nir->info.gs.invocations *
                stages[MESA_SHADER_GEOMETRY].nir->info.gs.vertices_out >
             256) {
         /* Fallback to the legacy path if tessellation is
          * enabled with extreme geometry because
          * EN_MAX_VERT_OUT_PER_GS_INSTANCE doesn't work and it
          * might hang.
          */
         stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;

         /* GFX11+ requires NGG. */
         assert(device->physical_device->rad_info.gfx_level < GFX11);
      }

      gl_shader_stage last_xfb_stage = MESA_SHADER_VERTEX;

      for (int i = MESA_SHADER_VERTEX; i <= MESA_SHADER_GEOMETRY; i++) {
         if (stages[i].nir)
            last_xfb_stage = i;
      }

      bool uses_xfb = stages[last_xfb_stage].nir &&
                      stages[last_xfb_stage].nir->xfb_info;

      if (!device->physical_device->use_ngg_streamout && uses_xfb) {
         /* GFX11+ requires NGG. */
         assert(device->physical_device->rad_info.gfx_level < GFX11);

         if (stages[MESA_SHADER_TESS_CTRL].nir)
           stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;
         else
           stages[MESA_SHADER_VERTEX].info.is_ngg = false;
      }

      /* Determine if the pipeline is eligible for the NGG passthrough
       * mode. It can't be enabled for geometry shaders, for NGG
       * streamout or for vertex shaders that export the primitive ID
       * (this is checked later because we don't have the info here.)
       */
      if (!stages[MESA_SHADER_GEOMETRY].nir && !uses_xfb) {
         if (stages[MESA_SHADER_TESS_CTRL].nir && stages[MESA_SHADER_TESS_EVAL].info.is_ngg) {
            stages[MESA_SHADER_TESS_EVAL].info.is_ngg_passthrough = true;
         } else if (stages[MESA_SHADER_VERTEX].nir && stages[MESA_SHADER_VERTEX].info.is_ngg) {
            stages[MESA_SHADER_VERTEX].info.is_ngg_passthrough = true;
         }
      }
   }
}

static void
radv_fill_shader_info(struct radv_pipeline *pipeline,
                      struct radv_pipeline_layout *pipeline_layout,
                      const struct radv_pipeline_key *pipeline_key,
                      struct radv_pipeline_stage *stages,
                      gl_shader_stage last_vgt_api_stage)
{
   struct radv_device *device = pipeline->device;
   unsigned active_stages = 0;
   unsigned filled_stages = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir)
         active_stages |= (1 << i);
   }

   if (stages[MESA_SHADER_TESS_CTRL].nir) {
      stages[MESA_SHADER_VERTEX].info.vs.as_ls = true;
   }

   if (stages[MESA_SHADER_GEOMETRY].nir) {
      if (stages[MESA_SHADER_TESS_CTRL].nir)
         stages[MESA_SHADER_TESS_EVAL].info.tes.as_es = true;
      else
         stages[MESA_SHADER_VERTEX].info.vs.as_es = true;
   }

   if (stages[MESA_SHADER_FRAGMENT].nir) {
      radv_nir_shader_info_init(&stages[MESA_SHADER_FRAGMENT].info);
      radv_nir_shader_info_pass(device, stages[MESA_SHADER_FRAGMENT].nir, pipeline_layout,
                                pipeline_key, &stages[MESA_SHADER_FRAGMENT].info);

      assert(last_vgt_api_stage != MESA_SHADER_NONE);
      struct radv_shader_info *pre_ps_info = &stages[last_vgt_api_stage].info;
      struct radv_vs_output_info *outinfo = NULL;
      if (last_vgt_api_stage == MESA_SHADER_VERTEX ||
          last_vgt_api_stage == MESA_SHADER_GEOMETRY) {
         outinfo = &pre_ps_info->vs.outinfo;
      } else if (last_vgt_api_stage == MESA_SHADER_TESS_EVAL) {
         outinfo = &pre_ps_info->tes.outinfo;
      } else if (last_vgt_api_stage == MESA_SHADER_MESH) {
         outinfo = &pre_ps_info->ms.outinfo;
      }

      /* Add PS input requirements to the output of the pre-PS stage. */
      bool ps_prim_id_in = stages[MESA_SHADER_FRAGMENT].info.ps.prim_id_input;
      bool ps_clip_dists_in = !!stages[MESA_SHADER_FRAGMENT].info.ps.num_input_clips_culls;

      assert(outinfo);
      outinfo->export_clip_dists |= ps_clip_dists_in;
      if (last_vgt_api_stage == MESA_SHADER_VERTEX ||
          last_vgt_api_stage == MESA_SHADER_TESS_EVAL) {
         outinfo->export_prim_id |= ps_prim_id_in;
      }

      filled_stages |= (1 << MESA_SHADER_FRAGMENT);
   }

   if (device->physical_device->rad_info.gfx_level >= GFX9 &&
       stages[MESA_SHADER_TESS_CTRL].nir) {
      struct nir_shader *combined_nir[] = {stages[MESA_SHADER_VERTEX].nir, stages[MESA_SHADER_TESS_CTRL].nir};

      radv_nir_shader_info_init(&stages[MESA_SHADER_TESS_CTRL].info);

      /* Copy data to merged stage. */
      stages[MESA_SHADER_TESS_CTRL].info.vs.as_ls = true;

      for (int i = 0; i < 2; i++) {
         radv_nir_shader_info_pass(device, combined_nir[i], pipeline_layout, pipeline_key,
                                   &stages[MESA_SHADER_TESS_CTRL].info);
      }

      filled_stages |= (1 << MESA_SHADER_VERTEX);
      filled_stages |= (1 << MESA_SHADER_TESS_CTRL);
   }

   if (device->physical_device->rad_info.gfx_level >= GFX9 &&
       stages[MESA_SHADER_GEOMETRY].nir) {
      gl_shader_stage pre_stage =
         stages[MESA_SHADER_TESS_EVAL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
      struct nir_shader *combined_nir[] = {stages[pre_stage].nir, stages[MESA_SHADER_GEOMETRY].nir};

      radv_nir_shader_info_init(&stages[MESA_SHADER_GEOMETRY].info);

      /* Copy data to merged stage. */
      if (pre_stage == MESA_SHADER_VERTEX) {
         stages[MESA_SHADER_GEOMETRY].info.vs.as_es = stages[MESA_SHADER_VERTEX].info.vs.as_es;
      } else {
         stages[MESA_SHADER_GEOMETRY].info.tes.as_es = stages[MESA_SHADER_TESS_EVAL].info.tes.as_es;
      }
      stages[MESA_SHADER_GEOMETRY].info.is_ngg = stages[pre_stage].info.is_ngg;
      stages[MESA_SHADER_GEOMETRY].info.gs.es_type = pre_stage;

      for (int i = 0; i < 2; i++) {
         radv_nir_shader_info_pass(device, combined_nir[i], pipeline_layout, pipeline_key,
                                   &stages[MESA_SHADER_GEOMETRY].info);
      }

      filled_stages |= (1 << pre_stage);
      filled_stages |= (1 << MESA_SHADER_GEOMETRY);
   }

   active_stages ^= filled_stages;
   while (active_stages) {
      int i = u_bit_scan(&active_stages);
      radv_nir_shader_info_init(&stages[i].info);
      radv_nir_shader_info_pass(device, stages[i].nir, pipeline_layout, pipeline_key,
                                &stages[i].info);
   }

   if (stages[MESA_SHADER_COMPUTE].nir) {
      unsigned subgroup_size = pipeline_key->cs.compute_subgroup_size;
      unsigned req_subgroup_size = subgroup_size;
      bool require_full_subgroups = pipeline_key->cs.require_full_subgroups;

      if (!subgroup_size)
         subgroup_size = device->physical_device->cs_wave_size;

      unsigned local_size = stages[MESA_SHADER_COMPUTE].nir->info.workgroup_size[0] *
                            stages[MESA_SHADER_COMPUTE].nir->info.workgroup_size[1] *
                            stages[MESA_SHADER_COMPUTE].nir->info.workgroup_size[2];

      /* Games don't always request full subgroups when they should,
       * which can cause bugs if cswave32 is enabled.
       */
      if (device->physical_device->cs_wave_size == 32 &&
          stages[MESA_SHADER_COMPUTE].nir->info.cs.uses_wide_subgroup_intrinsics && !req_subgroup_size &&
          local_size % RADV_SUBGROUP_SIZE == 0)
         require_full_subgroups = true;

      if (require_full_subgroups && !req_subgroup_size) {
         /* don't use wave32 pretending to be wave64 */
         subgroup_size = RADV_SUBGROUP_SIZE;
      }

      stages[MESA_SHADER_COMPUTE].info.cs.subgroup_size = subgroup_size;
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir) {
         stages[i].info.wave_size = radv_get_wave_size(device, i, &stages[i].info);
         stages[i].info.ballot_bit_size = radv_get_ballot_bit_size(device, i, &stages[i].info);
      }
   }

   /* PS always operates without workgroups. */
   if (stages[MESA_SHADER_FRAGMENT].nir)
      stages[MESA_SHADER_FRAGMENT].info.workgroup_size = stages[MESA_SHADER_FRAGMENT].info.wave_size;

   if (stages[MESA_SHADER_COMPUTE].nir) {
      /* Variable workgroup size is not supported by Vulkan. */
      assert(!stages[MESA_SHADER_COMPUTE].nir->info.workgroup_size_variable);

      stages[MESA_SHADER_COMPUTE].info.workgroup_size =
         ac_compute_cs_workgroup_size(
            stages[MESA_SHADER_COMPUTE].nir->info.workgroup_size, false, UINT32_MAX);
   }

   if (stages[MESA_SHADER_TASK].nir) {
      /* Task/mesh I/O uses the task ring buffers. */
      stages[MESA_SHADER_TASK].info.cs.uses_task_rings = true;
      stages[MESA_SHADER_MESH].info.cs.uses_task_rings = true;

      stages[MESA_SHADER_TASK].info.workgroup_size =
         ac_compute_cs_workgroup_size(
            stages[MESA_SHADER_TASK].nir->info.workgroup_size, false, UINT32_MAX);
   }
}

static void
radv_declare_pipeline_args(struct radv_device *device, struct radv_pipeline_stage *stages,
                           const struct radv_pipeline_key *pipeline_key)
{
   enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   unsigned active_stages = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir)
         active_stages |= (1 << i);
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      stages[i].args.is_gs_copy_shader = false;
      stages[i].args.explicit_scratch_args = !radv_use_llvm_for_stage(device, i);
      stages[i].args.remap_spi_ps_input = !radv_use_llvm_for_stage(device, i);
      stages[i].args.load_grid_size_from_user_sgpr = device->load_grid_size_from_user_sgpr;
   }

   if (gfx_level >= GFX9 && stages[MESA_SHADER_TESS_CTRL].nir) {
      radv_declare_shader_args(gfx_level, pipeline_key, &stages[MESA_SHADER_TESS_CTRL].info,
                               MESA_SHADER_TESS_CTRL, true, MESA_SHADER_VERTEX,
                               &stages[MESA_SHADER_TESS_CTRL].args);
      stages[MESA_SHADER_TESS_CTRL].info.user_sgprs_locs = stages[MESA_SHADER_TESS_CTRL].args.user_sgprs_locs;
      stages[MESA_SHADER_TESS_CTRL].info.inline_push_constant_mask =
         stages[MESA_SHADER_TESS_CTRL].args.ac.inline_push_const_mask;

      stages[MESA_SHADER_VERTEX].args = stages[MESA_SHADER_TESS_CTRL].args;
      active_stages &= ~(1 << MESA_SHADER_VERTEX);
      active_stages &= ~(1 << MESA_SHADER_TESS_CTRL);
   }

   if (gfx_level >= GFX9 && stages[MESA_SHADER_GEOMETRY].nir) {
      gl_shader_stage pre_stage =
         stages[MESA_SHADER_TESS_EVAL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
      radv_declare_shader_args(gfx_level, pipeline_key, &stages[MESA_SHADER_GEOMETRY].info,
                               MESA_SHADER_GEOMETRY, true, pre_stage,
                               &stages[MESA_SHADER_GEOMETRY].args);
      stages[MESA_SHADER_GEOMETRY].info.user_sgprs_locs = stages[MESA_SHADER_GEOMETRY].args.user_sgprs_locs;
      stages[MESA_SHADER_GEOMETRY].info.inline_push_constant_mask =
         stages[MESA_SHADER_GEOMETRY].args.ac.inline_push_const_mask;

      stages[pre_stage].args = stages[MESA_SHADER_GEOMETRY].args;
      active_stages &= ~(1 << pre_stage);
      active_stages &= ~(1 << MESA_SHADER_GEOMETRY);
   }

   u_foreach_bit(i, active_stages) {
      radv_declare_shader_args(gfx_level, pipeline_key, &stages[i].info, i, false,
                               MESA_SHADER_VERTEX, &stages[i].args);
      stages[i].info.user_sgprs_locs = stages[i].args.user_sgprs_locs;
      stages[i].info.inline_push_constant_mask = stages[i].args.ac.inline_push_const_mask;
   }
}

static void
merge_tess_info(struct shader_info *tes_info, struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 || tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED ||
          tes_info->tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;

   /* Copy the merged info back to the TCS */
   tcs_info->tess.tcs_vertices_out = tes_info->tess.tcs_vertices_out;
   tcs_info->tess.spacing = tes_info->tess.spacing;
   tcs_info->tess._primitive_mode = tes_info->tess._primitive_mode;
   tcs_info->tess.ccw = tes_info->tess.ccw;
   tcs_info->tess.point_mode = tes_info->tess.point_mode;
}

static void
gather_tess_info(struct radv_device *device, struct radv_pipeline_stage *stages,
                 const struct radv_pipeline_key *pipeline_key)
{
   merge_tess_info(&stages[MESA_SHADER_TESS_EVAL].nir->info,
                   &stages[MESA_SHADER_TESS_CTRL].nir->info);

   unsigned tess_in_patch_size = pipeline_key->tcs.tess_input_vertices;
   unsigned tess_out_patch_size = stages[MESA_SHADER_TESS_CTRL].nir->info.tess.tcs_vertices_out;

   /* Number of tessellation patches per workgroup processed by the current pipeline. */
   unsigned num_patches = get_tcs_num_patches(
      tess_in_patch_size, tess_out_patch_size,
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_inputs,
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_outputs,
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_patch_outputs,
      device->physical_device->hs.tess_offchip_block_dw_size, device->physical_device->rad_info.gfx_level,
      device->physical_device->rad_info.family);

   /* LDS size used by VS+TCS for storing TCS inputs and outputs. */
   unsigned tcs_lds_size = calculate_tess_lds_size(
      device->physical_device->rad_info.gfx_level, tess_in_patch_size, tess_out_patch_size,
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_inputs, num_patches,
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_outputs,
      stages[MESA_SHADER_TESS_CTRL].info.tcs.num_linked_patch_outputs);

   stages[MESA_SHADER_TESS_CTRL].info.num_tess_patches = num_patches;
   stages[MESA_SHADER_TESS_CTRL].info.tcs.num_lds_blocks = tcs_lds_size;
   stages[MESA_SHADER_TESS_CTRL].info.tcs.tes_reads_tess_factors =
      !!(stages[MESA_SHADER_TESS_EVAL].nir->info.inputs_read &
         (VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER));
   stages[MESA_SHADER_TESS_CTRL].info.tcs.tes_inputs_read = stages[MESA_SHADER_TESS_EVAL].nir->info.inputs_read;
   stages[MESA_SHADER_TESS_CTRL].info.tcs.tes_patch_inputs_read =
      stages[MESA_SHADER_TESS_EVAL].nir->info.patch_inputs_read;

   stages[MESA_SHADER_TESS_EVAL].info.num_tess_patches = num_patches;
   stages[MESA_SHADER_GEOMETRY].info.num_tess_patches = num_patches;
   stages[MESA_SHADER_VERTEX].info.num_tess_patches = num_patches;
   stages[MESA_SHADER_TESS_CTRL].info.tcs.tcs_vertices_out = tess_out_patch_size;
   stages[MESA_SHADER_VERTEX].info.tcs.tcs_vertices_out = tess_out_patch_size;

   if (!radv_use_llvm_for_stage(device, MESA_SHADER_VERTEX)) {
      /* When the number of TCS input and output vertices are the same (typically 3):
       * - There is an equal amount of LS and HS invocations
       * - In case of merged LSHS shaders, the LS and HS halves of the shader
       *   always process the exact same vertex. We can use this knowledge to optimize them.
       *
       * We don't set tcs_in_out_eq if the float controls differ because that might
       * involve different float modes for the same block and our optimizer
       * doesn't handle a instruction dominating another with a different mode.
       */
      stages[MESA_SHADER_VERTEX].info.vs.tcs_in_out_eq =
         device->physical_device->rad_info.gfx_level >= GFX9 &&
         tess_in_patch_size == tess_out_patch_size &&
         stages[MESA_SHADER_VERTEX].nir->info.float_controls_execution_mode ==
            stages[MESA_SHADER_TESS_CTRL].nir->info.float_controls_execution_mode;

      if (stages[MESA_SHADER_VERTEX].info.vs.tcs_in_out_eq)
         stages[MESA_SHADER_VERTEX].info.vs.tcs_temp_only_input_mask =
            stages[MESA_SHADER_TESS_CTRL].nir->info.inputs_read &
            stages[MESA_SHADER_VERTEX].nir->info.outputs_written &
            ~stages[MESA_SHADER_TESS_CTRL].nir->info.tess.tcs_cross_invocation_inputs_read &
            ~stages[MESA_SHADER_TESS_CTRL].nir->info.inputs_read_indirectly &
            ~stages[MESA_SHADER_VERTEX].nir->info.outputs_accessed_indirectly;

      /* Copy data to TCS so it can be accessed by the backend if they are merged. */
      stages[MESA_SHADER_TESS_CTRL].info.vs.tcs_in_out_eq = stages[MESA_SHADER_VERTEX].info.vs.tcs_in_out_eq;
      stages[MESA_SHADER_TESS_CTRL].info.vs.tcs_temp_only_input_mask =
         stages[MESA_SHADER_VERTEX].info.vs.tcs_temp_only_input_mask;
   }

   for (gl_shader_stage s = MESA_SHADER_VERTEX; s <= MESA_SHADER_TESS_CTRL; ++s)
      stages[s].info.workgroup_size =
         ac_compute_lshs_workgroup_size(device->physical_device->rad_info.gfx_level, s, num_patches,
                                        tess_in_patch_size, tess_out_patch_size);
}

static bool
mem_vectorize_callback(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                       unsigned num_components, nir_intrinsic_instr *low, nir_intrinsic_instr *high,
                       void *data)
{
   if (num_components > 4)
      return false;

   /* >128 bit loads are split except with SMEM */
   if (bit_size * num_components > 128)
      return false;

   uint32_t align;
   if (align_offset)
      align = 1 << (ffs(align_offset) - 1);
   else
      align = align_mul;

   switch (low->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_push_constant: {
      unsigned max_components;
      if (align % 4 == 0)
         max_components = NIR_MAX_VEC_COMPONENTS;
      else if (align % 2 == 0)
         max_components = 16u / bit_size;
      else
         max_components = 8u / bit_size;
      return (align % (bit_size / 8u)) == 0 && num_components <= max_components;
   }
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
      assert(nir_deref_mode_is(nir_src_as_deref(low->src[0]), nir_var_mem_shared));
      FALLTHROUGH;
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
      if (bit_size * num_components ==
          96) { /* 96 bit loads require 128 bit alignment and are split otherwise */
         return align % 16 == 0;
      } else if (bit_size == 16 && (align % 4)) {
         /* AMD hardware can't do 2-byte aligned f16vec2 loads, but they are useful for ALU
          * vectorization, because our vectorizer requires the scalar IR to already contain vectors.
          */
         return (align % 2 == 0) && num_components <= 2;
      } else {
         if (num_components == 3) {
            /* AMD hardware can't do 3-component loads except for 96-bit loads, handled above. */
            return false;
         }
         unsigned req = bit_size * num_components;
         if (req == 64 || req == 128) /* 64-bit and 128-bit loads can use ds_read2_b{32,64} */
            req /= 2u;
         return align % (req / 8u) == 0;
      }
   default:
      return false;
   }
   return false;
}

static unsigned
lower_bit_size_callback(const nir_instr *instr, void *_)
{
   struct radv_device *device = _;
   enum amd_gfx_level chip = device->physical_device->rad_info.gfx_level;

   if (instr->type != nir_instr_type_alu)
      return 0;
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   /* If an instruction is not scalarized by this point,
    * it can be emitted as packed instruction */
   if (alu->dest.dest.ssa.num_components > 1)
      return 0;

   if (alu->dest.dest.ssa.bit_size & (8 | 16)) {
      unsigned bit_size = alu->dest.dest.ssa.bit_size;
      switch (alu->op) {
      case nir_op_bitfield_select:
      case nir_op_imul_high:
      case nir_op_umul_high:
         return 32;
      case nir_op_iabs:
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_imin:
      case nir_op_umin:
      case nir_op_ishr:
      case nir_op_ushr:
      case nir_op_ishl:
      case nir_op_isign:
      case nir_op_uadd_sat:
      case nir_op_usub_sat:
         return (bit_size == 8 || !(chip >= GFX8 && nir_dest_is_divergent(alu->dest.dest))) ? 32
                                                                                            : 0;
      case nir_op_iadd_sat:
      case nir_op_isub_sat:
         return bit_size == 8 || !nir_dest_is_divergent(alu->dest.dest) ? 32 : 0;

      default:
         return 0;
      }
   }

   if (nir_src_bit_size(alu->src[0].src) & (8 | 16)) {
      unsigned bit_size = nir_src_bit_size(alu->src[0].src);
      switch (alu->op) {
      case nir_op_bit_count:
      case nir_op_find_lsb:
      case nir_op_ufind_msb:
      case nir_op_i2b1:
         return 32;
      case nir_op_ilt:
      case nir_op_ige:
      case nir_op_ieq:
      case nir_op_ine:
      case nir_op_ult:
      case nir_op_uge:
         return (bit_size == 8 || !(chip >= GFX8 && nir_dest_is_divergent(alu->dest.dest))) ? 32
                                                                                            : 0;
      default:
         return 0;
      }
   }

   return 0;
}

static uint8_t
opt_vectorize_callback(const nir_instr *instr, const void *_)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   const struct radv_device *device = _;
   enum amd_gfx_level chip = device->physical_device->rad_info.gfx_level;
   if (chip < GFX9)
      return 1;

   const nir_alu_instr *alu = nir_instr_as_alu(instr);
   const unsigned bit_size = alu->dest.dest.ssa.bit_size;
   if (bit_size != 16)
      return 1;

   switch (alu->op) {
   case nir_op_fadd:
   case nir_op_fsub:
   case nir_op_fmul:
   case nir_op_ffma:
   case nir_op_fdiv:
   case nir_op_flrp:
   case nir_op_fabs:
   case nir_op_fneg:
   case nir_op_fsat:
   case nir_op_fmin:
   case nir_op_fmax:
   case nir_op_iabs:
   case nir_op_iadd:
   case nir_op_iadd_sat:
   case nir_op_uadd_sat:
   case nir_op_isub:
   case nir_op_isub_sat:
   case nir_op_usub_sat:
   case nir_op_ineg:
   case nir_op_imul:
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
      return 2;
   case nir_op_ishl: /* TODO: in NIR, these have 32bit shift operands */
   case nir_op_ishr: /* while Radeon needs 16bit operands when vectorized */
   case nir_op_ushr:
   default:
      return 1;
   }
}

static nir_component_mask_t
non_uniform_access_callback(const nir_src *src, void *_)
{
   if (src->ssa->num_components == 1)
      return 0x1;
   return nir_chase_binding(*src).success ? 0x2 : 0x3;
}


VkResult
radv_upload_shaders(struct radv_device *device, struct radv_pipeline *pipeline,
                    struct radv_shader_binary **binaries, struct radv_shader_binary *gs_copy_binary)
{
   uint32_t code_size = 0;

   /* Compute the total code size. */
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      struct radv_shader *shader = pipeline->shaders[i];
      if (!shader)
         continue;

      code_size += align(shader->code_size, RADV_SHADER_ALLOC_ALIGNMENT);
   }

   if (pipeline->gs_copy_shader) {
      code_size += align(pipeline->gs_copy_shader->code_size, RADV_SHADER_ALLOC_ALIGNMENT);
   }

   /* Allocate memory for all shader binaries. */
   pipeline->slab = radv_pipeline_slab_create(device, pipeline, code_size);
   if (!pipeline->slab)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pipeline->slab_bo = pipeline->slab->alloc->arena->bo;

   /* Upload shader binaries. */
   uint64_t slab_va = radv_buffer_get_va(pipeline->slab_bo);
   uint32_t slab_offset = pipeline->slab->alloc->offset;
   char *slab_ptr = pipeline->slab->alloc->arena->ptr;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      struct radv_shader *shader = pipeline->shaders[i];
      if (!shader)
         continue;

      shader->va = slab_va + slab_offset;

      void *dest_ptr = slab_ptr + slab_offset;
      if (!radv_shader_binary_upload(device, binaries[i], shader, dest_ptr))
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      slab_offset += align(shader->code_size, RADV_SHADER_ALLOC_ALIGNMENT);
   }

   if (pipeline->gs_copy_shader) {
      pipeline->gs_copy_shader->va = slab_va + slab_offset;

      void *dest_ptr = slab_ptr + slab_offset;
      if (!radv_shader_binary_upload(device, gs_copy_binary, pipeline->gs_copy_shader, dest_ptr))
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

static bool
radv_consider_force_vrs(const struct radv_pipeline *pipeline, bool noop_fs,
                        const struct radv_pipeline_stage *stages,
                        gl_shader_stage last_vgt_api_stage)
{
   struct radv_device *device = pipeline->device;

   if (!device->force_vrs_enabled)
      return false;

   if (last_vgt_api_stage != MESA_SHADER_VERTEX &&
       last_vgt_api_stage != MESA_SHADER_TESS_EVAL &&
       last_vgt_api_stage != MESA_SHADER_GEOMETRY)
      return false;

   nir_shader *last_vgt_shader = stages[last_vgt_api_stage].nir;
   if (last_vgt_shader->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_SHADING_RATE))
      return false;

   /* VRS has no effect if there is no pixel shader. */
   if (noop_fs)
      return false;

   /* Do not enable if the PS uses gl_FragCoord because it breaks postprocessing in some games. */
   nir_shader *fs_shader = stages[MESA_SHADER_FRAGMENT].nir;
   if (fs_shader &&
       BITSET_TEST(fs_shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD)) {
      return false;
   }

   return true;
}

static nir_ssa_def *
radv_adjust_vertex_fetch_alpha(nir_builder *b,
                               enum radv_vs_input_alpha_adjust alpha_adjust,
                               nir_ssa_def *alpha)
{
   if (alpha_adjust == ALPHA_ADJUST_SSCALED)
      alpha = nir_f2u32(b, alpha);

   /* For the integer-like cases, do a natural sign extension.
    *
    * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0 and happen to contain 0, 1, 2, 3 as
    * the two LSBs of the exponent.
    */
   unsigned offset = alpha_adjust == ALPHA_ADJUST_SNORM ? 23u : 0u;

   alpha = nir_ibfe_imm(b, alpha, offset, 2u);

   /* Convert back to the right type. */
   if (alpha_adjust == ALPHA_ADJUST_SNORM) {
      alpha = nir_i2f32(b, alpha);
      alpha = nir_fmax(b, alpha, nir_imm_float(b, -1.0f));
   } else if (alpha_adjust == ALPHA_ADJUST_SSCALED) {
      alpha = nir_i2f32(b, alpha);
   }

   return alpha;
}

static bool
radv_lower_vs_input(nir_shader *nir, const struct radv_pipeline_key *pipeline_key)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   if (pipeline_key->vs.dynamic_input_state)
      return false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_load_input)
            continue;

         unsigned location = nir_intrinsic_base(intrin) - VERT_ATTRIB_GENERIC0;
         enum radv_vs_input_alpha_adjust alpha_adjust = pipeline_key->vs.vertex_alpha_adjust[location];
         bool post_shuffle = pipeline_key->vs.vertex_post_shuffle & (1 << location);

         unsigned component = nir_intrinsic_component(intrin);
         unsigned num_components = intrin->dest.ssa.num_components;

         unsigned attrib_format = pipeline_key->vs.vertex_attribute_formats[location];
         unsigned dfmt = attrib_format & 0xf;
         unsigned nfmt = (attrib_format >> 4) & 0x7;
         const struct ac_data_format_info *vtx_info = ac_get_data_format_info(dfmt);
         bool is_float =
            nfmt != V_008F0C_BUF_NUM_FORMAT_UINT && nfmt != V_008F0C_BUF_NUM_FORMAT_SINT;

         unsigned mask = nir_ssa_def_components_read(&intrin->dest.ssa) << component;
         unsigned num_channels = MIN2(util_last_bit(mask), vtx_info->num_channels);

         static const unsigned swizzle_normal[4] = {0, 1, 2, 3};
         static const unsigned swizzle_post_shuffle[4] = {2, 1, 0, 3};
         const unsigned *swizzle = post_shuffle ? swizzle_post_shuffle : swizzle_normal;

         b.cursor = nir_after_instr(instr);
         nir_ssa_def *channels[4];

         if (post_shuffle) {
            /* Expand to load 3 components because it's shuffled like X<->Z. */
            intrin->num_components = MAX2(component + num_components, 3);
            intrin->dest.ssa.num_components = intrin->num_components;

            nir_intrinsic_set_component(intrin, 0);

            num_channels = MAX2(num_channels, 3);
         }

         for (uint32_t i = 0; i < num_components; i++) {
            unsigned idx = i + (post_shuffle ? component : 0);

            if (swizzle[i + component] < num_channels) {
               channels[i] = nir_channel(&b, &intrin->dest.ssa, swizzle[idx]);
            } else if (i + component == 3) {
               channels[i] = is_float ? nir_imm_floatN_t(&b, 1.0f, intrin->dest.ssa.bit_size)
                                      : nir_imm_intN_t(&b, 1u, intrin->dest.ssa.bit_size);
            } else {
               channels[i] = nir_imm_zero(&b, 1, intrin->dest.ssa.bit_size);
            }
         }

         if (alpha_adjust != ALPHA_ADJUST_NONE && component + num_components == 4) {
            unsigned idx = num_components - 1;
            channels[idx] = radv_adjust_vertex_fetch_alpha(&b, alpha_adjust, channels[idx]);
         }

         nir_ssa_def *new_dest = nir_vec(&b, channels, num_components);

         nir_ssa_def_rewrite_uses_after(&intrin->dest.ssa, new_dest,
                                        new_dest->parent_instr);

         progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

static bool
radv_lower_fs_output(nir_shader *nir, const struct radv_pipeline_key *pipeline_key)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_store_output)
            continue;

         int slot = nir_intrinsic_base(intrin) - FRAG_RESULT_DATA0;
         if (slot < 0)
            continue;

         unsigned write_mask = nir_intrinsic_write_mask(intrin);
         unsigned col_format = (pipeline_key->ps.col_format >> (4 * slot)) & 0xf;
         bool is_int8 = (pipeline_key->ps.is_int8 >> slot) & 1;
         bool is_int10 = (pipeline_key->ps.is_int10 >> slot) & 1;
         bool enable_mrt_output_nan_fixup = (pipeline_key->ps.enable_mrt_output_nan_fixup >> slot) & 1;
         bool is_16bit = intrin->src[0].ssa->bit_size == 16;

         if (col_format == V_028714_SPI_SHADER_ZERO)
            continue;

         b.cursor = nir_before_instr(instr);
         nir_ssa_def *values[4];

         /* Extract the export values. */
         for (unsigned i = 0; i < 4; i++) {
            if (write_mask & (1 << i)) {
               values[i] = nir_channel(&b, intrin->src[0].ssa, i);
            } else {
               values[i] = nir_ssa_undef(&b, 1, 32);
            }
         }

         /* Replace NaN by zero (for 32-bit float formats) to fix game bugs if requested. */
         if (enable_mrt_output_nan_fixup && !nir->info.internal && !is_16bit) {
            u_foreach_bit(i, write_mask) {
               const bool save_exact = b.exact;

               b.exact = true;
               nir_ssa_def *isnan = nir_fneu(&b, values[i], values[i]);
               b.exact = save_exact;

               values[i] = nir_bcsel(&b, isnan, nir_imm_zero(&b, 1, 32), values[i]);
            }
         }

         if (col_format == V_028714_SPI_SHADER_FP16_ABGR ||
             col_format == V_028714_SPI_SHADER_UNORM16_ABGR ||
             col_format == V_028714_SPI_SHADER_SNORM16_ABGR ||
             col_format == V_028714_SPI_SHADER_UINT16_ABGR ||
             col_format == V_028714_SPI_SHADER_SINT16_ABGR) {
            /* Convert and/or clamp the export values. */
            switch (col_format) {
            case V_028714_SPI_SHADER_UINT16_ABGR: {
               unsigned max_rgb = is_int8 ? 255 : is_int10 ? 1023 : 0;
               u_foreach_bit(i, write_mask) {
                  if (is_int8 || is_int10) {
                     values[i] = nir_umin(&b, values[i], i == 3 && is_int10 ? nir_imm_int(&b, 3u)
                                                                            : nir_imm_int(&b, max_rgb));
                  } else if (is_16bit) {
                     values[i] = nir_u2u32(&b, values[i]);
                  }
               }
               break;
            }
            case V_028714_SPI_SHADER_SINT16_ABGR: {
               unsigned max_rgb = is_int8 ? 127 : is_int10 ? 511 : 0;
               unsigned min_rgb = is_int8 ? -128 : is_int10 ? -512 : 0;
               u_foreach_bit(i, write_mask) {
                  if (is_int8 || is_int10) {
                     values[i] = nir_imin(&b, values[i], i == 3 && is_int10 ? nir_imm_int(&b, 1u)
                                                                            : nir_imm_int(&b, max_rgb));
                     values[i] = nir_imax(&b, values[i], i == 3 && is_int10 ? nir_imm_int(&b, -2u)
                                                                            : nir_imm_int(&b, min_rgb));
                  } else if (is_16bit) {
                     values[i] = nir_i2i32(&b, values[i]);
                  }
               }
               break;
            }
            case V_028714_SPI_SHADER_UNORM16_ABGR:
            case V_028714_SPI_SHADER_SNORM16_ABGR:
               u_foreach_bit(i, write_mask) {
                  if (is_16bit) {
                     values[i] = nir_f2f32(&b, values[i]);
                  }
               }
               break;
            default:
               break;
            }

            /* Only nir_pack_32_2x16_split needs 16-bit inputs. */
            bool input_16_bit = col_format == V_028714_SPI_SHADER_FP16_ABGR && is_16bit;
            unsigned new_write_mask = 0;

            /* Pack the export values. */
            for (unsigned i = 0; i < 2; i++) {
               bool enabled = (write_mask >> (i * 2)) & 0x3;

               if (!enabled) {
                  values[i] = nir_ssa_undef(&b, 1, 32);
                  continue;
               }

               nir_ssa_def *src0 = values[i * 2];
               nir_ssa_def *src1 = values[i * 2 + 1];

               if (!(write_mask & (1 << (i * 2))))
                  src0 = nir_imm_zero(&b, 1, input_16_bit ? 16 : 32);
               if (!(write_mask & (1 << (i * 2 + 1))))
                  src1 = nir_imm_zero(&b, 1, input_16_bit ? 16 : 32);

               if (col_format == V_028714_SPI_SHADER_FP16_ABGR) {
                  if (is_16bit) {
                     values[i] = nir_pack_32_2x16_split(&b, src0, src1);
                  } else {
                     values[i] = nir_pack_half_2x16_split(&b, src0, src1);
                  }
               } else if (col_format == V_028714_SPI_SHADER_UNORM16_ABGR) {
                  values[i] = nir_pack_unorm_2x16(&b, nir_vec2(&b, src0, src1));
               } else if (col_format == V_028714_SPI_SHADER_SNORM16_ABGR) {
                  values[i] = nir_pack_snorm_2x16(&b, nir_vec2(&b, src0, src1));
               } else if (col_format == V_028714_SPI_SHADER_UINT16_ABGR) {
                  values[i] = nir_pack_uint_2x16(&b, nir_vec2(&b, src0, src1));
               } else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR) {
                  values[i] = nir_pack_sint_2x16(&b, nir_vec2(&b, src0, src1));
               }

               new_write_mask |= 1 << i;
            }

            /* Update the write mask for compressed outputs. */
            nir_intrinsic_set_write_mask(intrin, new_write_mask);
            intrin->num_components = util_last_bit(new_write_mask);
         }

         nir_ssa_def *new_src = nir_vec(&b, values, intrin->num_components);

         nir_instr_rewrite_src(&intrin->instr, &intrin->src[0], nir_src_for_ssa(new_src));

         progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

void
radv_pipeline_stage_init(const VkPipelineShaderStageCreateInfo *sinfo,
                         struct radv_pipeline_stage *out_stage, gl_shader_stage stage)
{
   const VkShaderModuleCreateInfo *minfo =
      vk_find_struct_const(sinfo->pNext, SHADER_MODULE_CREATE_INFO);
   const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *iinfo =
      vk_find_struct_const(sinfo->pNext, PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);

   if (sinfo->module == VK_NULL_HANDLE && !minfo && !iinfo)
      return;

   memset(out_stage, 0, sizeof(*out_stage));

   out_stage->stage = stage;
   out_stage->entrypoint = sinfo->pName;
   out_stage->spec_info = sinfo->pSpecializationInfo;
   out_stage->feedback.flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;

   if (sinfo->module != VK_NULL_HANDLE) {
      struct vk_shader_module *module = vk_shader_module_from_handle(sinfo->module);
      STATIC_ASSERT(sizeof(out_stage->spirv.sha1) == sizeof(module->sha1));

      out_stage->spirv.data = module->data;
      out_stage->spirv.size = module->size;
      out_stage->spirv.object = &module->base;

      if (module->nir)
         out_stage->internal_nir = module->nir;
   } else if (minfo) {
      out_stage->spirv.data = (const char *) minfo->pCode;
      out_stage->spirv.size = minfo->codeSize;
   }

   vk_pipeline_hash_shader_stage(sinfo, out_stage->shader_sha1);
}

static struct radv_shader *
radv_pipeline_create_gs_copy_shader(struct radv_pipeline *pipeline,
                                    struct radv_pipeline_stage *stages,
                                    const struct radv_pipeline_key *pipeline_key,
                                    const struct radv_pipeline_layout *pipeline_layout,
                                    bool keep_executable_info, bool keep_statistic_info,
                                    struct radv_shader_binary **gs_copy_binary)
{
   struct radv_device *device = pipeline->device;
   struct radv_shader_info info = {0};

   if (stages[MESA_SHADER_GEOMETRY].info.vs.outinfo.export_clip_dists)
      info.vs.outinfo.export_clip_dists = true;

   radv_nir_shader_info_pass(device, stages[MESA_SHADER_GEOMETRY].nir, pipeline_layout, pipeline_key,
                             &info);
   info.wave_size = 64; /* Wave32 not supported. */
   info.workgroup_size = 64; /* HW VS: separate waves, no workgroups */
   info.ballot_bit_size = 64;

   struct radv_shader_args gs_copy_args = {0};
   gs_copy_args.is_gs_copy_shader = true;
   gs_copy_args.explicit_scratch_args = !radv_use_llvm_for_stage(device, MESA_SHADER_VERTEX);
   radv_declare_shader_args(device->physical_device->rad_info.gfx_level, pipeline_key, &info,
                            MESA_SHADER_VERTEX, false, MESA_SHADER_VERTEX, &gs_copy_args);
   info.user_sgprs_locs = gs_copy_args.user_sgprs_locs;
   info.inline_push_constant_mask = gs_copy_args.ac.inline_push_const_mask;

   return radv_create_gs_copy_shader(device, stages[MESA_SHADER_GEOMETRY].nir, &info, &gs_copy_args,
                                     gs_copy_binary, keep_executable_info, keep_statistic_info,
                                     pipeline_key->optimisations_disabled);
}

static void
radv_pipeline_nir_to_asm(struct radv_pipeline *pipeline, struct radv_pipeline_stage *stages,
                         const struct radv_pipeline_key *pipeline_key,
                         const struct radv_pipeline_layout *pipeline_layout,
                         bool keep_executable_info, bool keep_statistic_info,
                         gl_shader_stage last_vgt_api_stage,
                         struct radv_shader_binary **binaries,
                         struct radv_shader_binary **gs_copy_binary)
{
   struct radv_device *device = pipeline->device;
   unsigned active_stages = 0;

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir)
         active_stages |= (1 << i);
   }

   bool pipeline_has_ngg = last_vgt_api_stage != MESA_SHADER_NONE &&
                           stages[last_vgt_api_stage].info.is_ngg;

   if (stages[MESA_SHADER_GEOMETRY].nir && !pipeline_has_ngg) {
      pipeline->gs_copy_shader =
         radv_pipeline_create_gs_copy_shader(pipeline, stages, pipeline_key, pipeline_layout,
                                             keep_executable_info, keep_statistic_info,
                                             gs_copy_binary);
   }

   for (int s = MESA_VULKAN_SHADER_STAGES - 1; s >= 0; s--) {
      if (!(active_stages & (1 << s)) || pipeline->shaders[s])
         continue;

      nir_shader *shaders[2] = { stages[s].nir, NULL };
      unsigned shader_count = 1;

      /* On GFX9+, TES is merged with GS and VS is merged with TCS or GS. */
      if (device->physical_device->rad_info.gfx_level >= GFX9 &&
          (s == MESA_SHADER_TESS_CTRL || s == MESA_SHADER_GEOMETRY)) {
         gl_shader_stage pre_stage;

         if (s == MESA_SHADER_GEOMETRY && stages[MESA_SHADER_TESS_EVAL].nir) {
            pre_stage = MESA_SHADER_TESS_EVAL;
         } else {
            pre_stage = MESA_SHADER_VERTEX;
         }

         shaders[0] = stages[pre_stage].nir;
         shaders[1] = stages[s].nir;
         shader_count = 2;
      }

      int64_t stage_start = os_time_get_nano();

      pipeline->shaders[s] = radv_shader_nir_to_asm(device, &stages[s], shaders, shader_count,
                                                    pipeline_key, keep_executable_info,
                                                    keep_statistic_info, &binaries[s]);

      stages[s].feedback.duration += os_time_get_nano() - stage_start;

      active_stages &= ~(1 << shaders[0]->info.stage);
      if (shaders[1])
         active_stages &= ~(1 << shaders[1]->info.stage);
   }
}

VkResult
radv_create_shaders(struct radv_pipeline *pipeline, struct radv_pipeline_layout *pipeline_layout,
                    struct radv_device *device, struct radv_pipeline_cache *cache,
                    const struct radv_pipeline_key *pipeline_key,
                    const VkPipelineShaderStageCreateInfo *pStages,
                    uint32_t stageCount,
                    const VkPipelineCreateFlags flags, const uint8_t *custom_hash,
                    const VkPipelineCreationFeedbackCreateInfo *creation_feedback,
                    struct radv_pipeline_shader_stack_size **stack_sizes,
                    uint32_t *num_stack_sizes,
                    gl_shader_stage *last_vgt_api_stage)
{
   const char *noop_fs_entrypoint = "noop_fs";
   struct radv_shader_binary *binaries[MESA_VULKAN_SHADER_STAGES] = {NULL};
   struct radv_shader_binary *gs_copy_binary = NULL;
   unsigned char hash[20];
   bool keep_executable_info =
      (flags & VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR) ||
      device->keep_shader_info;
   bool keep_statistic_info = (flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR) ||
                              (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) ||
                              device->keep_shader_info;
   struct radv_pipeline_stage stages[MESA_VULKAN_SHADER_STAGES] = {0};
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   bool noop_fs = false;
   VkResult result = VK_SUCCESS;

   int64_t pipeline_start = os_time_get_nano();

   for (uint32_t i = 0; i < stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pStages[i];
      gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);

      radv_pipeline_stage_init(sinfo, &stages[stage], stage);
   }

   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (!stages[s].entrypoint)
         continue;

      if (stages[s].stage < MESA_SHADER_FRAGMENT || stages[s].stage == MESA_SHADER_MESH)
         *last_vgt_api_stage = stages[s].stage;
   }

   ASSERTED bool primitive_shading =
      stages[MESA_SHADER_VERTEX].entrypoint || stages[MESA_SHADER_TESS_CTRL].entrypoint ||
      stages[MESA_SHADER_TESS_EVAL].entrypoint || stages[MESA_SHADER_GEOMETRY].entrypoint;
   ASSERTED bool mesh_shading =
      stages[MESA_SHADER_MESH].entrypoint;

   /* Primitive and mesh shading must not be mixed in the same pipeline. */
   assert(!primitive_shading || !mesh_shading);
   /* Mesh shaders are mandatory in mesh shading pipelines. */
   assert(mesh_shading == !!stages[MESA_SHADER_MESH].entrypoint);
   /* Mesh shaders always need NGG. */
   assert(!mesh_shading || pipeline_key->use_ngg);

   if (custom_hash)
      memcpy(hash, custom_hash, 20);
   else {
      radv_hash_shaders(hash, stages, pipeline_layout, pipeline_key,
                        radv_get_hash_flags(device, keep_statistic_info));
   }

   pipeline->pipeline_hash = *(uint64_t *)hash;

   bool found_in_application_cache = true;
   if (!keep_executable_info &&
       radv_create_shaders_from_pipeline_cache(device, cache, hash, pipeline,
                                               stack_sizes, num_stack_sizes,
                                               &found_in_application_cache)) {
      if (found_in_application_cache)
         pipeline_feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      result = VK_SUCCESS;
      goto done;
   }

   if (flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
      if (found_in_application_cache)
         pipeline_feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      result = VK_PIPELINE_COMPILE_REQUIRED;
      goto done;
   }

   if (pipeline->type == RADV_PIPELINE_GRAPHICS && !stages[MESA_SHADER_FRAGMENT].entrypoint) {
      nir_builder fs_b = radv_meta_init_shader(device, MESA_SHADER_FRAGMENT, "noop_fs");

      stages[MESA_SHADER_FRAGMENT] = (struct radv_pipeline_stage) {
         .stage = MESA_SHADER_FRAGMENT,
         .internal_nir = fs_b.shader,
         .entrypoint = noop_fs_entrypoint,
         .feedback = {
            .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
         },
      };

      noop_fs = true;
   }

   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (!stages[s].entrypoint)
         continue;

      int64_t stage_start = os_time_get_nano();

      stages[s].nir = radv_shader_spirv_to_nir(device, &stages[s], pipeline_key);

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }

   /* Force per-vertex VRS. */
   if (radv_consider_force_vrs(pipeline, noop_fs, stages, *last_vgt_api_stage)) {
      assert(*last_vgt_api_stage == MESA_SHADER_VERTEX ||
             *last_vgt_api_stage == MESA_SHADER_TESS_EVAL ||
             *last_vgt_api_stage == MESA_SHADER_GEOMETRY);
      nir_shader *last_vgt_shader = stages[*last_vgt_api_stage].nir;
      NIR_PASS(_, last_vgt_shader, radv_force_primitive_shading_rate, device);
   }

   bool optimize_conservatively = pipeline_key->optimisations_disabled;

   /* Determine if shaders uses NGG before linking because it's needed for some NIR pass. */
   radv_fill_shader_info_ngg(pipeline, pipeline_key, stages);

   bool pipeline_has_ngg = (stages[MESA_SHADER_VERTEX].nir && stages[MESA_SHADER_VERTEX].info.is_ngg) ||
                           (stages[MESA_SHADER_TESS_EVAL].nir && stages[MESA_SHADER_TESS_EVAL].info.is_ngg) ||
                           (stages[MESA_SHADER_MESH].nir && stages[MESA_SHADER_MESH].info.is_ngg);

   if (stages[MESA_SHADER_GEOMETRY].nir) {
      unsigned nir_gs_flags = nir_lower_gs_intrinsics_per_stream;

      if (pipeline_has_ngg) {
         nir_gs_flags |= nir_lower_gs_intrinsics_count_primitives |
                         nir_lower_gs_intrinsics_count_vertices_per_primitive |
                         nir_lower_gs_intrinsics_overwrite_incomplete;
      }

      NIR_PASS(_, stages[MESA_SHADER_GEOMETRY].nir, nir_lower_gs_intrinsics, nir_gs_flags);
   }

   radv_link_shaders(pipeline, pipeline_key, stages, optimize_conservatively, *last_vgt_api_stage);
   radv_set_driver_locations(pipeline, stages, *last_vgt_api_stage);

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (stages[i].nir) {
         int64_t stage_start = os_time_get_nano();

         radv_optimize_nir(stages[i].nir, optimize_conservatively, false);

         /* Gather info again, information such as outputs_read can be out-of-date. */
         nir_shader_gather_info(stages[i].nir, nir_shader_get_entrypoint(stages[i].nir));
         radv_lower_io(device, stages[i].nir, stages[MESA_SHADER_MESH].nir);

         stages[i].feedback.duration += os_time_get_nano() - stage_start;
      }
   }

   if (stages[MESA_SHADER_TESS_CTRL].nir) {
      nir_lower_patch_vertices(stages[MESA_SHADER_TESS_EVAL].nir,
                               stages[MESA_SHADER_TESS_CTRL].nir->info.tess.tcs_vertices_out, NULL);
      gather_tess_info(device, stages, pipeline_key);
   }

   if (stages[MESA_SHADER_VERTEX].nir) {
      NIR_PASS(_, stages[MESA_SHADER_VERTEX].nir, radv_lower_vs_input, pipeline_key);
   }

   if (stages[MESA_SHADER_FRAGMENT].nir && !radv_use_llvm_for_stage(device, MESA_SHADER_FRAGMENT)) {
      /* TODO: Convert the LLVM backend. */
      NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, radv_lower_fs_output, pipeline_key);
   }

   radv_fill_shader_info(pipeline, pipeline_layout, pipeline_key, stages, *last_vgt_api_stage);

   if (pipeline_has_ngg) {
      struct gfx10_ngg_info *ngg_info;

      if (stages[MESA_SHADER_GEOMETRY].nir)
         ngg_info = &stages[MESA_SHADER_GEOMETRY].info.ngg_info;
      else if (stages[MESA_SHADER_TESS_CTRL].nir)
         ngg_info = &stages[MESA_SHADER_TESS_EVAL].info.ngg_info;
      else if (stages[MESA_SHADER_VERTEX].nir)
         ngg_info = &stages[MESA_SHADER_VERTEX].info.ngg_info;
      else if (stages[MESA_SHADER_MESH].nir)
         ngg_info = &stages[MESA_SHADER_MESH].info.ngg_info;
      else
         unreachable("Missing NGG shader stage.");

      if (*last_vgt_api_stage == MESA_SHADER_MESH)
         gfx10_get_ngg_ms_info(&stages[MESA_SHADER_MESH], ngg_info);
      else
         gfx10_get_ngg_info(pipeline_key, pipeline, stages, ngg_info);
   } else if (stages[MESA_SHADER_GEOMETRY].nir) {
      struct gfx9_gs_info *gs_info = &stages[MESA_SHADER_GEOMETRY].info.gs_ring_info;

      gfx9_get_gs_info(pipeline_key, pipeline, stages, gs_info);
   } else {
      gl_shader_stage hw_vs_api_stage =
         stages[MESA_SHADER_TESS_EVAL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
      stages[hw_vs_api_stage].info.workgroup_size = stages[hw_vs_api_stage].info.wave_size;
   }

   radv_determine_ngg_settings(pipeline, pipeline_key, stages, *last_vgt_api_stage);

   radv_declare_pipeline_args(device, stages, pipeline_key);

   if (stages[MESA_SHADER_FRAGMENT].nir) {
      NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, radv_lower_fs_intrinsics,
               &stages[MESA_SHADER_FRAGMENT], pipeline_key);
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (stages[i].nir) {
         int64_t stage_start = os_time_get_nano();

         /* Wave and workgroup size should already be filled. */
         assert(stages[i].info.wave_size && stages[i].info.workgroup_size);

         if (!radv_use_llvm_for_stage(device, i)) {
            nir_lower_non_uniform_access_options options = {
               .types = nir_lower_non_uniform_ubo_access | nir_lower_non_uniform_ssbo_access |
                        nir_lower_non_uniform_texture_access | nir_lower_non_uniform_image_access,
               .callback = &non_uniform_access_callback,
               .callback_data = NULL,
            };
            NIR_PASS(_, stages[i].nir, nir_lower_non_uniform_access, &options);
         }
         NIR_PASS(_, stages[i].nir, nir_lower_memory_model);

         nir_load_store_vectorize_options vectorize_opts = {
            .modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_push_const |
                     nir_var_mem_shared | nir_var_mem_global,
            .callback = mem_vectorize_callback,
            .robust_modes = 0,
            /* On GFX6, read2/write2 is out-of-bounds if the offset register is negative, even if
             * the final offset is not.
             */
            .has_shared2_amd = device->physical_device->rad_info.gfx_level >= GFX7,
         };

         if (device->robust_buffer_access2) {
            vectorize_opts.robust_modes =
               nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_push_const;
         }

         bool progress = false;
         NIR_PASS(progress, stages[i].nir, nir_opt_load_store_vectorize, &vectorize_opts);
         if (progress) {
            NIR_PASS(_, stages[i].nir, nir_copy_prop);
            NIR_PASS(_, stages[i].nir, nir_opt_shrink_stores,
                     !device->instance->disable_shrink_image_store);

            /* Gather info again, to update whether 8/16-bit are used. */
            nir_shader_gather_info(stages[i].nir, nir_shader_get_entrypoint(stages[i].nir));
         }

         struct radv_shader_info *info = &stages[i].info;
         if (pipeline->device->physical_device->rad_info.gfx_level >= GFX9) {
            if (i == MESA_SHADER_VERTEX && stages[MESA_SHADER_TESS_CTRL].nir)
               info = &stages[MESA_SHADER_TESS_CTRL].info;
            else if (i == MESA_SHADER_VERTEX && stages[MESA_SHADER_GEOMETRY].nir)
               info = &stages[MESA_SHADER_GEOMETRY].info;
            else if (i == MESA_SHADER_TESS_EVAL && stages[MESA_SHADER_GEOMETRY].nir)
               info = &stages[MESA_SHADER_GEOMETRY].info;
         }
         NIR_PASS(_, stages[i].nir, radv_nir_lower_ycbcr_textures, pipeline_layout);
         NIR_PASS_V(stages[i].nir, radv_nir_apply_pipeline_layout, device, pipeline_layout, info,
                    &stages[i].args);

         NIR_PASS(_, stages[i].nir, nir_opt_shrink_vectors);

         NIR_PASS(_, stages[i].nir, nir_lower_alu_width, opt_vectorize_callback, device);

         /* lower ALU operations */
         NIR_PASS(_, stages[i].nir, nir_lower_int64);

         NIR_PASS(_, stages[i].nir, nir_opt_idiv_const, 8);

         NIR_PASS(_, stages[i].nir, nir_lower_idiv,
                  &(nir_lower_idiv_options){
                     .imprecise_32bit_lowering = false,
                     .allow_fp16 = device->physical_device->rad_info.gfx_level >= GFX9,
                  });

         nir_move_options sink_opts = nir_move_const_undef | nir_move_copies;
         if (i != MESA_SHADER_FRAGMENT || !pipeline_key->disable_sinking_load_input_fs)
            sink_opts |= nir_move_load_input;

         NIR_PASS(_, stages[i].nir, nir_opt_sink, sink_opts);
         NIR_PASS(_, stages[i].nir, nir_opt_move,
                  nir_move_load_input | nir_move_const_undef | nir_move_copies);

         /* Lower I/O intrinsics to memory instructions. */
         bool io_to_mem = radv_lower_io_to_mem(device, &stages[i], pipeline_key);
         bool lowered_ngg = pipeline_has_ngg && i == *last_vgt_api_stage;
         if (lowered_ngg)
            radv_lower_ngg(device, &stages[i], pipeline_key);

         NIR_PASS(_, stages[i].nir, ac_nir_lower_global_access);
         NIR_PASS_V(stages[i].nir, radv_nir_lower_abi, device->physical_device->rad_info.gfx_level,
                    &stages[i].info, &stages[i].args, pipeline_key,
                    radv_use_llvm_for_stage(device, i));
         radv_optimize_nir_algebraic(
            stages[i].nir, io_to_mem || lowered_ngg || i == MESA_SHADER_COMPUTE || i == MESA_SHADER_TASK);

         if (stages[i].nir->info.bit_sizes_int & (8 | 16)) {
            if (device->physical_device->rad_info.gfx_level >= GFX8) {
               NIR_PASS(_, stages[i].nir, nir_convert_to_lcssa, true, true);
               nir_divergence_analysis(stages[i].nir);
            }

            if (nir_lower_bit_size(stages[i].nir, lower_bit_size_callback, device)) {
               NIR_PASS(_, stages[i].nir, nir_opt_constant_folding);
            }

            if (device->physical_device->rad_info.gfx_level >= GFX8)
               NIR_PASS(_, stages[i].nir, nir_opt_remove_phis); /* cleanup LCSSA phis */
         }
         if (((stages[i].nir->info.bit_sizes_int | stages[i].nir->info.bit_sizes_float) & 16) &&
             device->physical_device->rad_info.gfx_level >= GFX9) {
            bool separate_g16 = device->physical_device->rad_info.gfx_level >= GFX10;
            struct nir_fold_tex_srcs_options fold_srcs_options[] = {
               {
                  .sampler_dims =
                     ~(BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE) | BITFIELD_BIT(GLSL_SAMPLER_DIM_BUF)),
                  .src_types = (1 << nir_tex_src_coord) | (1 << nir_tex_src_lod) |
                               (1 << nir_tex_src_bias) | (1 << nir_tex_src_min_lod) |
                               (1 << nir_tex_src_ms_index) |
                               (separate_g16 ? 0 : (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy)),
               },
               {
                  .sampler_dims = ~BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE),
                  .src_types = (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy),
               },
            };
            struct nir_fold_16bit_tex_image_options fold_16bit_options = {
               .rounding_mode = nir_rounding_mode_rtne,
               .fold_tex_dest = true,
               .fold_image_load_store_data = true,
               .fold_srcs_options_count = separate_g16 ? 2 : 1,
               .fold_srcs_options = fold_srcs_options,
            };
            NIR_PASS(_, stages[i].nir, nir_fold_16bit_tex_image, &fold_16bit_options);

            NIR_PASS(_, stages[i].nir, nir_opt_vectorize, opt_vectorize_callback, device);
         }

         /* cleanup passes */
         NIR_PASS(_, stages[i].nir, nir_lower_alu_width, opt_vectorize_callback, device);
         NIR_PASS(_, stages[i].nir, nir_lower_load_const_to_scalar);
         NIR_PASS(_, stages[i].nir, nir_copy_prop);
         NIR_PASS(_, stages[i].nir, nir_opt_dce);

         sink_opts |= nir_move_comparisons | nir_move_load_ubo | nir_move_load_ssbo;
         NIR_PASS(_, stages[i].nir, nir_opt_sink, sink_opts);

         nir_move_options move_opts = nir_move_const_undef | nir_move_load_ubo |
                                      nir_move_load_input | nir_move_comparisons | nir_move_copies;
         NIR_PASS(_, stages[i].nir, nir_opt_move, move_opts);

         stages[i].feedback.duration += os_time_get_nano() - stage_start;
      }
   }

   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (stages[i].nir) {
         if (radv_can_dump_shader(device, stages[i].nir, false))
            nir_print_shader(stages[i].nir, stderr);
      }
   }

   /* Compile NIR shaders to AMD assembly. */
   radv_pipeline_nir_to_asm(pipeline, stages, pipeline_key, pipeline_layout, keep_executable_info,
                            keep_statistic_info, *last_vgt_api_stage, binaries, &gs_copy_binary);

   if (keep_executable_info) {
      for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
         struct radv_shader *shader = pipeline->shaders[i];
         if (!shader)
            continue;

         if (!stages[i].spirv.size)
            continue;

         shader->spirv = malloc(stages[i].spirv.size);
         memcpy(shader->spirv, stages[i].spirv.data, stages[i].spirv.size);
         shader->spirv_size = stages[i].spirv.size;
      }
   }

   /* Upload shader binaries. */
   radv_upload_shaders(device, pipeline, binaries, gs_copy_binary);

   if (!keep_executable_info) {
      if (pipeline->gs_copy_shader) {
         assert(!binaries[MESA_SHADER_COMPUTE] && !pipeline->shaders[MESA_SHADER_COMPUTE]);
         binaries[MESA_SHADER_COMPUTE] = gs_copy_binary;
         pipeline->shaders[MESA_SHADER_COMPUTE] = pipeline->gs_copy_shader;
      }

      radv_pipeline_cache_insert_shaders(device, cache, hash, pipeline, binaries,
                                         stack_sizes ? *stack_sizes : NULL,
                                         num_stack_sizes ? *num_stack_sizes : 0);

      if (pipeline->gs_copy_shader) {
         pipeline->gs_copy_shader = pipeline->shaders[MESA_SHADER_COMPUTE];
         pipeline->shaders[MESA_SHADER_COMPUTE] = NULL;
         binaries[MESA_SHADER_COMPUTE] = NULL;
      }
   }

   free(gs_copy_binary);
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      free(binaries[i]);
      if (stages[i].nir) {
         if (radv_can_dump_shader_stats(device, stages[i].nir) && pipeline->shaders[i]) {
            radv_dump_shader_stats(device, pipeline, i, stderr);
         }

         ralloc_free(stages[i].nir);
      }
   }

done:
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      uint32_t stage_count = creation_feedback->pipelineStageCreationFeedbackCount;
      assert(stage_count == 0 || stageCount == stage_count);
      for (uint32_t i = 0; i < stage_count; i++) {
         gl_shader_stage s = vk_to_mesa_shader_stage(pStages[i].stage);
         creation_feedback->pPipelineStageCreationFeedbacks[i] = stages[s].feedback;
      }
   }

   return result;
}

static uint32_t
radv_pipeline_stage_to_user_data_0(struct radv_graphics_pipeline *pipeline, gl_shader_stage stage,
                                   enum amd_gfx_level gfx_level)
{
   bool has_gs = radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY);
   bool has_tess = radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL);
   bool has_ngg = radv_pipeline_has_ngg(pipeline);

   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      return R_00B030_SPI_SHADER_USER_DATA_PS_0;
   case MESA_SHADER_VERTEX:
      if (has_tess) {
         if (gfx_level >= GFX10) {
            return R_00B430_SPI_SHADER_USER_DATA_HS_0;
         } else if (gfx_level == GFX9) {
            return R_00B430_SPI_SHADER_USER_DATA_LS_0;
         } else {
            return R_00B530_SPI_SHADER_USER_DATA_LS_0;
         }
      }

      if (has_gs) {
         if (gfx_level >= GFX10) {
            return R_00B230_SPI_SHADER_USER_DATA_GS_0;
         } else {
            return R_00B330_SPI_SHADER_USER_DATA_ES_0;
         }
      }

      if (has_ngg)
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;

      return R_00B130_SPI_SHADER_USER_DATA_VS_0;
   case MESA_SHADER_GEOMETRY:
      return gfx_level == GFX9 ? R_00B330_SPI_SHADER_USER_DATA_ES_0
                               : R_00B230_SPI_SHADER_USER_DATA_GS_0;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      return R_00B900_COMPUTE_USER_DATA_0;
   case MESA_SHADER_TESS_CTRL:
      return gfx_level == GFX9 ? R_00B430_SPI_SHADER_USER_DATA_LS_0
                               : R_00B430_SPI_SHADER_USER_DATA_HS_0;
   case MESA_SHADER_TESS_EVAL:
      if (has_gs) {
         return gfx_level >= GFX10 ? R_00B230_SPI_SHADER_USER_DATA_GS_0
                                   : R_00B330_SPI_SHADER_USER_DATA_ES_0;
      } else if (has_ngg) {
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;
      } else {
         return R_00B130_SPI_SHADER_USER_DATA_VS_0;
      }
   case MESA_SHADER_MESH:
      assert(has_ngg);
      return R_00B230_SPI_SHADER_USER_DATA_GS_0;
   default:
      unreachable("unknown shader");
   }
}

struct radv_bin_size_entry {
   unsigned bpp;
   VkExtent2D extent;
};

static VkExtent2D
radv_gfx9_compute_bin_size(const struct radv_graphics_pipeline *pipeline,
                           const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   static const struct radv_bin_size_entry color_size_table[][3][9] = {
      {
         /* One RB / SE */
         {
            /* One shader engine */
            {0, {128, 128}},
            {1, {64, 128}},
            {2, {32, 128}},
            {3, {16, 128}},
            {17, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Two shader engines */
            {0, {128, 128}},
            {2, {64, 128}},
            {3, {32, 128}},
            {5, {16, 128}},
            {17, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Four shader engines */
            {0, {128, 128}},
            {3, {64, 128}},
            {5, {16, 128}},
            {17, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         /* Two RB / SE */
         {
            /* One shader engine */
            {0, {128, 128}},
            {2, {64, 128}},
            {3, {32, 128}},
            {5, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Two shader engines */
            {0, {128, 128}},
            {3, {64, 128}},
            {5, {32, 128}},
            {9, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Four shader engines */
            {0, {256, 256}},
            {2, {128, 256}},
            {3, {128, 128}},
            {5, {64, 128}},
            {9, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         /* Four RB / SE */
         {
            /* One shader engine */
            {0, {128, 256}},
            {2, {128, 128}},
            {3, {64, 128}},
            {5, {32, 128}},
            {9, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Two shader engines */
            {0, {256, 256}},
            {2, {128, 256}},
            {3, {128, 128}},
            {5, {64, 128}},
            {9, {32, 128}},
            {17, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            /* Four shader engines */
            {0, {256, 512}},
            {2, {256, 256}},
            {3, {128, 256}},
            {5, {128, 128}},
            {9, {64, 128}},
            {17, {16, 128}},
            {33, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
   };
   static const struct radv_bin_size_entry ds_size_table[][3][9] = {
      {
         // One RB / SE
         {
            // One shader engine
            {0, {128, 256}},
            {2, {128, 128}},
            {4, {64, 128}},
            {7, {32, 128}},
            {13, {16, 128}},
            {49, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Two shader engines
            {0, {256, 256}},
            {2, {128, 256}},
            {4, {128, 128}},
            {7, {64, 128}},
            {13, {32, 128}},
            {25, {16, 128}},
            {49, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Four shader engines
            {0, {256, 512}},
            {2, {256, 256}},
            {4, {128, 256}},
            {7, {128, 128}},
            {13, {64, 128}},
            {25, {16, 128}},
            {49, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         // Two RB / SE
         {
            // One shader engine
            {0, {256, 256}},
            {2, {128, 256}},
            {4, {128, 128}},
            {7, {64, 128}},
            {13, {32, 128}},
            {25, {16, 128}},
            {97, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Two shader engines
            {0, {256, 512}},
            {2, {256, 256}},
            {4, {128, 256}},
            {7, {128, 128}},
            {13, {64, 128}},
            {25, {32, 128}},
            {49, {16, 128}},
            {97, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Four shader engines
            {0, {512, 512}},
            {2, {256, 512}},
            {4, {256, 256}},
            {7, {128, 256}},
            {13, {128, 128}},
            {25, {64, 128}},
            {49, {16, 128}},
            {97, {0, 0}},
            {UINT_MAX, {0, 0}},
         },
      },
      {
         // Four RB / SE
         {
            // One shader engine
            {0, {256, 512}},
            {2, {256, 256}},
            {4, {128, 256}},
            {7, {128, 128}},
            {13, {64, 128}},
            {25, {32, 128}},
            {49, {16, 128}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Two shader engines
            {0, {512, 512}},
            {2, {256, 512}},
            {4, {256, 256}},
            {7, {128, 256}},
            {13, {128, 128}},
            {25, {64, 128}},
            {49, {32, 128}},
            {97, {16, 128}},
            {UINT_MAX, {0, 0}},
         },
         {
            // Four shader engines
            {0, {512, 512}},
            {4, {256, 512}},
            {7, {256, 256}},
            {13, {128, 256}},
            {25, {128, 128}},
            {49, {64, 128}},
            {97, {16, 128}},
            {UINT_MAX, {0, 0}},
         },
      },
   };

   VkExtent2D extent = {512, 512};

   unsigned log_num_rb_per_se =
      util_logbase2_ceil(pdevice->rad_info.max_render_backends / pdevice->rad_info.max_se);
   unsigned log_num_se = util_logbase2_ceil(pdevice->rad_info.max_se);

   unsigned total_samples = 1u << G_028BE0_MSAA_NUM_SAMPLES(pipeline->ms.pa_sc_aa_config);
   unsigned ps_iter_samples = 1u << G_028804_PS_ITER_SAMPLES(pipeline->ms.db_eqaa);
   unsigned effective_samples = total_samples;
   unsigned color_bytes_per_pixel = 0;

   for (unsigned i = 0; i < info->ri.color_att_count; i++) {
      if (!info->cb.att[i].color_write_mask)
         continue;

      if (info->ri.color_att_formats[i] == VK_FORMAT_UNDEFINED)
         continue;

      color_bytes_per_pixel += vk_format_get_blocksize(info->ri.color_att_formats[i]);
   }

   /* MSAA images typically don't use all samples all the time. */
   if (effective_samples >= 2 && ps_iter_samples <= 1)
      effective_samples = 2;
   color_bytes_per_pixel *= effective_samples;

   const struct radv_bin_size_entry *color_entry = color_size_table[log_num_rb_per_se][log_num_se];
   while (color_entry[1].bpp <= color_bytes_per_pixel)
      ++color_entry;

   extent = color_entry->extent;

   if (radv_pipeline_has_ds_attachments(&info->ri)) {
      /* Coefficients taken from AMDVLK */
      unsigned depth_coeff = info->ri.depth_att_format != VK_FORMAT_UNDEFINED ? 5 : 0;
      unsigned stencil_coeff = info->ri.stencil_att_format != VK_FORMAT_UNDEFINED ? 1 : 0;
      unsigned ds_bytes_per_pixel = 4 * (depth_coeff + stencil_coeff) * total_samples;

      const struct radv_bin_size_entry *ds_entry = ds_size_table[log_num_rb_per_se][log_num_se];
      while (ds_entry[1].bpp <= ds_bytes_per_pixel)
         ++ds_entry;

      if (ds_entry->extent.width * ds_entry->extent.height < extent.width * extent.height)
         extent = ds_entry->extent;
   }

   return extent;
}

static VkExtent2D
radv_gfx10_compute_bin_size(const struct radv_graphics_pipeline *pipeline,
                            const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   VkExtent2D extent = {512, 512};

   const unsigned db_tag_size = 64;
   const unsigned db_tag_count = 312;
   const unsigned color_tag_size = 1024;
   const unsigned color_tag_count = 31;
   const unsigned fmask_tag_size = 256;
   const unsigned fmask_tag_count = 44;

   const unsigned rb_count = pdevice->rad_info.max_render_backends;
   const unsigned pipe_count = MAX2(rb_count, pdevice->rad_info.num_tcc_blocks);

   const unsigned db_tag_part = (db_tag_count * rb_count / pipe_count) * db_tag_size * pipe_count;
   const unsigned color_tag_part =
      (color_tag_count * rb_count / pipe_count) * color_tag_size * pipe_count;
   const unsigned fmask_tag_part =
      (fmask_tag_count * rb_count / pipe_count) * fmask_tag_size * pipe_count;

   const unsigned total_samples =
      1u << G_028BE0_MSAA_NUM_SAMPLES(pipeline->ms.pa_sc_aa_config);
   const unsigned samples_log = util_logbase2_ceil(total_samples);

   unsigned color_bytes_per_pixel = 0;
   unsigned fmask_bytes_per_pixel = 0;

   for (unsigned i = 0; i < info->ri.color_att_count; i++) {
      if (!info->cb.att[i].color_write_mask)
         continue;

      if (info->ri.color_att_formats[i] == VK_FORMAT_UNDEFINED)
         continue;

      color_bytes_per_pixel += vk_format_get_blocksize(info->ri.color_att_formats[i]);

      if (total_samples > 1) {
         assert(samples_log <= 3);
         const unsigned fmask_array[] = {0, 1, 1, 4};
         fmask_bytes_per_pixel += fmask_array[samples_log];
      }
   }

   color_bytes_per_pixel *= total_samples;
   color_bytes_per_pixel = MAX2(color_bytes_per_pixel, 1);

   const unsigned color_pixel_count_log = util_logbase2(color_tag_part / color_bytes_per_pixel);
   extent.width = 1ull << ((color_pixel_count_log + 1) / 2);
   extent.height = 1ull << (color_pixel_count_log / 2);

   if (fmask_bytes_per_pixel) {
      const unsigned fmask_pixel_count_log = util_logbase2(fmask_tag_part / fmask_bytes_per_pixel);

      const VkExtent2D fmask_extent =
         (VkExtent2D){.width = 1ull << ((fmask_pixel_count_log + 1) / 2),
                      .height = 1ull << (color_pixel_count_log / 2)};

      if (fmask_extent.width * fmask_extent.height < extent.width * extent.height)
         extent = fmask_extent;
   }

   if (radv_pipeline_has_ds_attachments(&info->ri)) {
      /* Coefficients taken from AMDVLK */
      unsigned depth_coeff = info->ri.depth_att_format != VK_FORMAT_UNDEFINED ? 5 : 0;
      unsigned stencil_coeff = info->ri.stencil_att_format != VK_FORMAT_UNDEFINED ? 1 : 0;
      unsigned db_bytes_per_pixel = (depth_coeff + stencil_coeff) * total_samples;

      const unsigned db_pixel_count_log = util_logbase2(db_tag_part / db_bytes_per_pixel);

      const VkExtent2D db_extent = (VkExtent2D){.width = 1ull << ((db_pixel_count_log + 1) / 2),
                                                .height = 1ull << (color_pixel_count_log / 2)};

      if (db_extent.width * db_extent.height < extent.width * extent.height)
         extent = db_extent;
   }

   extent.width = MAX2(extent.width, 128);
   extent.height = MAX2(extent.width, 64);

   return extent;
}

static void
radv_pipeline_init_disabled_binning_state(struct radv_graphics_pipeline *pipeline,
                                          const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint32_t pa_sc_binner_cntl_0 = S_028C44_BINNING_MODE(V_028C44_DISABLE_BINNING_USE_LEGACY_SC) |
                                  S_028C44_DISABLE_START_OF_PRIM(1);

   if (pdevice->rad_info.gfx_level >= GFX10) {
      unsigned min_bytes_per_pixel = 0;

      for (unsigned i = 0; i < info->ri.color_att_count; i++) {
         if (!info->cb.att[i].color_write_mask)
            continue;

         if (info->ri.color_att_formats[i] == VK_FORMAT_UNDEFINED)
            continue;

         unsigned bytes = vk_format_get_blocksize(info->ri.color_att_formats[i]);
         if (!min_bytes_per_pixel || bytes < min_bytes_per_pixel)
            min_bytes_per_pixel = bytes;
      }

      pa_sc_binner_cntl_0 =
         S_028C44_BINNING_MODE(V_028C44_DISABLE_BINNING_USE_NEW_SC) | S_028C44_BIN_SIZE_X(0) |
         S_028C44_BIN_SIZE_Y(0) | S_028C44_BIN_SIZE_X_EXTEND(2) |       /* 128 */
         S_028C44_BIN_SIZE_Y_EXTEND(min_bytes_per_pixel <= 4 ? 2 : 1) | /* 128 or 64 */
         S_028C44_DISABLE_START_OF_PRIM(1);
   }

   pipeline->binning.pa_sc_binner_cntl_0 = pa_sc_binner_cntl_0;
}

struct radv_binning_settings
radv_get_binning_settings(const struct radv_physical_device *pdev)
{
   struct radv_binning_settings settings;
   if (pdev->rad_info.has_dedicated_vram) {
      if (pdev->rad_info.max_render_backends > 4) {
         settings.context_states_per_bin = 1;
         settings.persistent_states_per_bin = 1;
      } else {
         settings.context_states_per_bin = 3;
         settings.persistent_states_per_bin = 8;
      }
      settings.fpovs_per_batch = 63;
   } else {
      /* The context states are affected by the scissor bug. */
      settings.context_states_per_bin = 6;
      /* 32 causes hangs for RAVEN. */
      settings.persistent_states_per_bin = 16;
      settings.fpovs_per_batch = 63;
   }

   if (pdev->rad_info.has_gfx9_scissor_bug)
      settings.context_states_per_bin = 1;

   return settings;
}

static void
radv_pipeline_init_binning_state(struct radv_graphics_pipeline *pipeline,
                                 const struct radv_blend_state *blend,
                                 const struct radv_graphics_pipeline_info *info)
{
   const struct radv_device *device = pipeline->base.device;

   if (device->physical_device->rad_info.gfx_level < GFX9)
      return;

   VkExtent2D bin_size;
   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      bin_size = radv_gfx10_compute_bin_size(pipeline, info);
   } else if (device->physical_device->rad_info.gfx_level == GFX9) {
      bin_size = radv_gfx9_compute_bin_size(pipeline, info);
   } else
      unreachable("Unhandled generation for binning bin size calculation");

   if (device->pbb_allowed && bin_size.width && bin_size.height) {
      struct radv_binning_settings settings = radv_get_binning_settings(device->physical_device);

      const uint32_t pa_sc_binner_cntl_0 =
         S_028C44_BINNING_MODE(V_028C44_BINNING_ALLOWED) |
         S_028C44_BIN_SIZE_X(bin_size.width == 16) | S_028C44_BIN_SIZE_Y(bin_size.height == 16) |
         S_028C44_BIN_SIZE_X_EXTEND(util_logbase2(MAX2(bin_size.width, 32)) - 5) |
         S_028C44_BIN_SIZE_Y_EXTEND(util_logbase2(MAX2(bin_size.height, 32)) - 5) |
         S_028C44_CONTEXT_STATES_PER_BIN(settings.context_states_per_bin - 1) |
         S_028C44_PERSISTENT_STATES_PER_BIN(settings.persistent_states_per_bin - 1) |
         S_028C44_DISABLE_START_OF_PRIM(1) |
         S_028C44_FPOVS_PER_BATCH(settings.fpovs_per_batch) | S_028C44_OPTIMAL_BIN_SELECTION(1);

      pipeline->binning.pa_sc_binner_cntl_0 = pa_sc_binner_cntl_0;
   } else
      radv_pipeline_init_disabled_binning_state(pipeline, info);
}

static void
radv_pipeline_emit_depth_stencil_state(struct radeon_cmdbuf *ctx_cs,
                                       const struct radv_depth_stencil_state *ds_state)
{
   radeon_set_context_reg(ctx_cs, R_028000_DB_RENDER_CONTROL, ds_state->db_render_control);

   radeon_set_context_reg_seq(ctx_cs, R_02800C_DB_RENDER_OVERRIDE, 2);
   radeon_emit(ctx_cs, ds_state->db_render_override);
   radeon_emit(ctx_cs, ds_state->db_render_override2);
}

static void
radv_pipeline_emit_blend_state(struct radeon_cmdbuf *ctx_cs,
                               const struct radv_graphics_pipeline *pipeline,
                               const struct radv_blend_state *blend)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;

   radeon_set_context_reg_seq(ctx_cs, R_028780_CB_BLEND0_CONTROL, 8);
   radeon_emit_array(ctx_cs, blend->cb_blend_control, 8);
   radeon_set_context_reg(ctx_cs, R_028B70_DB_ALPHA_TO_MASK, blend->db_alpha_to_mask);

   if (pdevice->rad_info.has_rbplus) {

      radeon_set_context_reg_seq(ctx_cs, R_028760_SX_MRT0_BLEND_OPT, 8);
      radeon_emit_array(ctx_cs, blend->sx_mrt_blend_opt, 8);
   }

   radeon_set_context_reg(ctx_cs, R_028714_SPI_SHADER_COL_FORMAT, blend->spi_shader_col_format);

   radeon_set_context_reg(ctx_cs, R_02823C_CB_SHADER_MASK, blend->cb_shader_mask);
}

static void
radv_pipeline_emit_raster_state(struct radeon_cmdbuf *ctx_cs,
                                const struct radv_graphics_pipeline *pipeline,
                                const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const VkConservativeRasterizationModeEXT mode = info->rs.conservative_mode;
   uint32_t pa_sc_conservative_rast = S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(1);

   if (pdevice->rad_info.gfx_level >= GFX9) {
      /* Conservative rasterization. */
      if (mode != VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
         pa_sc_conservative_rast = S_028C4C_PREZ_AA_MASK_ENABLE(1) | S_028C4C_POSTZ_AA_MASK_ENABLE(1) |
                                   S_028C4C_CENTROID_SAMPLE_OVERRIDE(1);

         if (mode == VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT) {
            pa_sc_conservative_rast |=
               S_028C4C_OVER_RAST_ENABLE(1) | S_028C4C_OVER_RAST_SAMPLE_SELECT(0) |
               S_028C4C_UNDER_RAST_ENABLE(0) | S_028C4C_UNDER_RAST_SAMPLE_SELECT(1) |
               S_028C4C_PBB_UNCERTAINTY_REGION_ENABLE(1);
         } else {
            assert(mode == VK_CONSERVATIVE_RASTERIZATION_MODE_UNDERESTIMATE_EXT);
            pa_sc_conservative_rast |=
               S_028C4C_OVER_RAST_ENABLE(0) | S_028C4C_OVER_RAST_SAMPLE_SELECT(1) |
               S_028C4C_UNDER_RAST_ENABLE(1) | S_028C4C_UNDER_RAST_SAMPLE_SELECT(0) |
               S_028C4C_PBB_UNCERTAINTY_REGION_ENABLE(0);
         }
      }

      radeon_set_context_reg(ctx_cs, R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
                             pa_sc_conservative_rast);
   }
}

static void
radv_pipeline_emit_multisample_state(struct radeon_cmdbuf *ctx_cs,
                                     const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct radv_multisample_state *ms = &pipeline->ms;

   radeon_set_context_reg_seq(ctx_cs, R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
   radeon_emit(ctx_cs, ms->pa_sc_aa_mask[0]);
   radeon_emit(ctx_cs, ms->pa_sc_aa_mask[1]);

   radeon_set_context_reg(ctx_cs, R_028804_DB_EQAA, ms->db_eqaa);
   radeon_set_context_reg(ctx_cs, R_028BE0_PA_SC_AA_CONFIG, ms->pa_sc_aa_config);

   radeon_set_context_reg_seq(ctx_cs, R_028A48_PA_SC_MODE_CNTL_0, 2);
   radeon_emit(ctx_cs, ms->pa_sc_mode_cntl_0);
   radeon_emit(ctx_cs, ms->pa_sc_mode_cntl_1);

   /* The exclusion bits can be set to improve rasterization efficiency
    * if no sample lies on the pixel boundary (-8 sample offset). It's
    * currently always TRUE because the driver doesn't support 16 samples.
    */
   bool exclusion = pdevice->rad_info.gfx_level >= GFX7;
   radeon_set_context_reg(
      ctx_cs, R_02882C_PA_SU_PRIM_FILTER_CNTL,
      S_02882C_XMAX_RIGHT_EXCLUSION(exclusion) | S_02882C_YMAX_BOTTOM_EXCLUSION(exclusion));
}

static void
radv_pipeline_emit_vgt_gs_mode(struct radeon_cmdbuf *ctx_cs,
                               const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   const struct radv_shader *vs = pipeline->base.shaders[MESA_SHADER_TESS_EVAL]
                                  ? pipeline->base.shaders[MESA_SHADER_TESS_EVAL]
                                  : pipeline->base.shaders[MESA_SHADER_VERTEX];
   unsigned vgt_primitiveid_en = 0;
   uint32_t vgt_gs_mode = 0;

   if (radv_pipeline_has_ngg(pipeline))
      return;

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      const struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];

      vgt_gs_mode = ac_vgt_gs_mode(gs->info.gs.vertices_out, pdevice->rad_info.gfx_level);
   } else if (outinfo->export_prim_id || vs->info.uses_prim_id) {
      vgt_gs_mode = S_028A40_MODE(V_028A40_GS_SCENARIO_A);
      vgt_primitiveid_en |= S_028A84_PRIMITIVEID_EN(1);
   }

   radeon_set_context_reg(ctx_cs, R_028A84_VGT_PRIMITIVEID_EN, vgt_primitiveid_en);
   radeon_set_context_reg(ctx_cs, R_028A40_VGT_GS_MODE, vgt_gs_mode);
}

static void
radv_pipeline_emit_hw_vs(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                         const struct radv_graphics_pipeline *pipeline, const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg_seq(cs, R_00B120_SPI_SHADER_PGM_LO_VS, 4);
   radeon_emit(cs, va >> 8);
   radeon_emit(cs, S_00B124_MEM_BASE(va >> 40));
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);

   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   unsigned clip_dist_mask, cull_dist_mask, total_mask;
   clip_dist_mask = outinfo->clip_dist_mask;
   cull_dist_mask = outinfo->cull_dist_mask;
   total_mask = clip_dist_mask | cull_dist_mask;

   bool misc_vec_ena = outinfo->writes_pointsize || outinfo->writes_layer ||
                       outinfo->writes_viewport_index || outinfo->writes_primitive_shading_rate;
   unsigned spi_vs_out_config, nparams;

   /* VS is required to export at least one param. */
   nparams = MAX2(outinfo->param_exports, 1);
   spi_vs_out_config = S_0286C4_VS_EXPORT_COUNT(nparams - 1);

   if (pdevice->rad_info.gfx_level >= GFX10) {
      spi_vs_out_config |= S_0286C4_NO_PC_EXPORT(outinfo->param_exports == 0);
   }

   radeon_set_context_reg(ctx_cs, R_0286C4_SPI_VS_OUT_CONFIG, spi_vs_out_config);

   radeon_set_context_reg(
      ctx_cs, R_02870C_SPI_SHADER_POS_FORMAT,
      S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
         S_02870C_POS1_EXPORT_FORMAT(outinfo->pos_exports > 1 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS2_EXPORT_FORMAT(outinfo->pos_exports > 2 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS3_EXPORT_FORMAT(outinfo->pos_exports > 3 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE));

   radeon_set_context_reg(ctx_cs, R_02881C_PA_CL_VS_OUT_CNTL,
                          S_02881C_USE_VTX_POINT_SIZE(outinfo->writes_pointsize) |
                             S_02881C_USE_VTX_RENDER_TARGET_INDX(outinfo->writes_layer) |
                             S_02881C_USE_VTX_VIEWPORT_INDX(outinfo->writes_viewport_index) |
                             S_02881C_USE_VTX_VRS_RATE(outinfo->writes_primitive_shading_rate) |
                             S_02881C_VS_OUT_MISC_VEC_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
                             S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
                             total_mask << 8 | clip_dist_mask);

   if (pdevice->rad_info.gfx_level <= GFX8)
      radeon_set_context_reg(ctx_cs, R_028AB4_VGT_REUSE_OFF, outinfo->writes_viewport_index);

   unsigned late_alloc_wave64, cu_mask;
   ac_compute_late_alloc(&pdevice->rad_info, false, false, shader->config.scratch_bytes_per_wave > 0,
                         &late_alloc_wave64, &cu_mask);

   if (pdevice->rad_info.gfx_level >= GFX7) {
      if (pdevice->rad_info.gfx_level >= GFX10) {
         ac_set_reg_cu_en(cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS,
                          S_00B118_CU_EN(cu_mask) | S_00B118_WAVE_LIMIT(0x3F),
                          C_00B118_CU_EN, 0, &pdevice->rad_info,
                          (void*)gfx10_set_sh_reg_idx3);
      } else {
         radeon_set_sh_reg_idx(pdevice, cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS, 3,
                               S_00B118_CU_EN(cu_mask) | S_00B118_WAVE_LIMIT(0x3F));
      }
      radeon_set_sh_reg(cs, R_00B11C_SPI_SHADER_LATE_ALLOC_VS, S_00B11C_LIMIT(late_alloc_wave64));
   }
   if (pdevice->rad_info.gfx_level >= GFX10) {
      uint32_t oversub_pc_lines = late_alloc_wave64 ? pdevice->rad_info.pc_lines / 4 : 0;
      gfx10_emit_ge_pc_alloc(cs, pdevice->rad_info.gfx_level, oversub_pc_lines);
   }
}

static void
radv_pipeline_emit_hw_es(struct radeon_cmdbuf *cs, const struct radv_graphics_pipeline *pipeline,
                         const struct radv_shader *shader)
{
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg_seq(cs, R_00B320_SPI_SHADER_PGM_LO_ES, 4);
   radeon_emit(cs, va >> 8);
   radeon_emit(cs, S_00B324_MEM_BASE(va >> 40));
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);
}

static void
radv_pipeline_emit_hw_ls(struct radeon_cmdbuf *cs, const struct radv_graphics_pipeline *pipeline,
                         const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   unsigned num_lds_blocks = pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.tcs.num_lds_blocks;
   uint64_t va = radv_shader_get_va(shader);
   uint32_t rsrc2 = shader->config.rsrc2;

   radeon_set_sh_reg(cs, R_00B520_SPI_SHADER_PGM_LO_LS, va >> 8);

   rsrc2 |= S_00B52C_LDS_SIZE(num_lds_blocks);
   if (pdevice->rad_info.gfx_level == GFX7 && pdevice->rad_info.family != CHIP_HAWAII)
      radeon_set_sh_reg(cs, R_00B52C_SPI_SHADER_PGM_RSRC2_LS, rsrc2);

   radeon_set_sh_reg_seq(cs, R_00B528_SPI_SHADER_PGM_RSRC1_LS, 2);
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, rsrc2);
}

static void
radv_pipeline_emit_hw_ngg(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                          const struct radv_graphics_pipeline *pipeline,
                          const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint64_t va = radv_shader_get_va(shader);
   gl_shader_stage es_type =
      radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH) ? MESA_SHADER_MESH :
      radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL) ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
   struct radv_shader *es = pipeline->base.shaders[es_type];
   const struct gfx10_ngg_info *ngg_state = &shader->info.ngg_info;

   radeon_set_sh_reg(cs, R_00B320_SPI_SHADER_PGM_LO_ES, va >> 8);

   radeon_set_sh_reg_seq(cs, R_00B228_SPI_SHADER_PGM_RSRC1_GS, 2);
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);

   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   unsigned clip_dist_mask, cull_dist_mask, total_mask;
   clip_dist_mask = outinfo->clip_dist_mask;
   cull_dist_mask = outinfo->cull_dist_mask;
   total_mask = clip_dist_mask | cull_dist_mask;

   bool misc_vec_ena = outinfo->writes_pointsize || outinfo->writes_layer ||
                       outinfo->writes_viewport_index || outinfo->writes_primitive_shading_rate;
   bool es_enable_prim_id = outinfo->export_prim_id || (es && es->info.uses_prim_id);
   bool break_wave_at_eoi = false;
   unsigned ge_cntl;

   if (es_type == MESA_SHADER_TESS_EVAL) {
      struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];

      if (es_enable_prim_id || (gs && gs->info.uses_prim_id))
         break_wave_at_eoi = true;
   }

   bool no_pc_export = outinfo->param_exports == 0 && outinfo->prim_param_exports == 0;
   unsigned num_params = MAX2(outinfo->param_exports, 1);
   unsigned num_prim_params = outinfo->prim_param_exports;
   radeon_set_context_reg(
      ctx_cs, R_0286C4_SPI_VS_OUT_CONFIG,
      S_0286C4_VS_EXPORT_COUNT(num_params - 1) |
      S_0286C4_PRIM_EXPORT_COUNT(num_prim_params) |
      S_0286C4_NO_PC_EXPORT(no_pc_export));

   unsigned idx_format = V_028708_SPI_SHADER_1COMP;
   if (outinfo->writes_layer_per_primitive ||
       outinfo->writes_viewport_index_per_primitive ||
       outinfo->writes_primitive_shading_rate_per_primitive)
      idx_format = V_028708_SPI_SHADER_2COMP;

   radeon_set_context_reg(ctx_cs, R_028708_SPI_SHADER_IDX_FORMAT,
                          S_028708_IDX0_EXPORT_FORMAT(idx_format));
   radeon_set_context_reg(
      ctx_cs, R_02870C_SPI_SHADER_POS_FORMAT,
      S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
         S_02870C_POS1_EXPORT_FORMAT(outinfo->pos_exports > 1 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS2_EXPORT_FORMAT(outinfo->pos_exports > 2 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE) |
         S_02870C_POS3_EXPORT_FORMAT(outinfo->pos_exports > 3 ? V_02870C_SPI_SHADER_4COMP
                                                              : V_02870C_SPI_SHADER_NONE));

   radeon_set_context_reg(ctx_cs, R_02881C_PA_CL_VS_OUT_CNTL,
                          S_02881C_USE_VTX_POINT_SIZE(outinfo->writes_pointsize) |
                             S_02881C_USE_VTX_RENDER_TARGET_INDX(outinfo->writes_layer) |
                             S_02881C_USE_VTX_VIEWPORT_INDX(outinfo->writes_viewport_index) |
                             S_02881C_USE_VTX_VRS_RATE(outinfo->writes_primitive_shading_rate) |
                             S_02881C_VS_OUT_MISC_VEC_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(misc_vec_ena) |
                             S_02881C_VS_OUT_CCDIST0_VEC_ENA((total_mask & 0x0f) != 0) |
                             S_02881C_VS_OUT_CCDIST1_VEC_ENA((total_mask & 0xf0) != 0) |
                             total_mask << 8 | clip_dist_mask);

   radeon_set_context_reg(ctx_cs, R_028A84_VGT_PRIMITIVEID_EN,
                          S_028A84_PRIMITIVEID_EN(es_enable_prim_id) |
                             S_028A84_NGG_DISABLE_PROVOK_REUSE(outinfo->export_prim_id));

   radeon_set_context_reg(ctx_cs, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
                          ngg_state->vgt_esgs_ring_itemsize);

   /* NGG specific registers. */
   struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];
   uint32_t gs_num_invocations = gs ? gs->info.gs.invocations : 1;

   if (pdevice->rad_info.gfx_level < GFX11) {
      radeon_set_context_reg(
         ctx_cs, R_028A44_VGT_GS_ONCHIP_CNTL,
         S_028A44_ES_VERTS_PER_SUBGRP(ngg_state->hw_max_esverts) |
            S_028A44_GS_PRIMS_PER_SUBGRP(ngg_state->max_gsprims) |
            S_028A44_GS_INST_PRIMS_IN_SUBGRP(ngg_state->max_gsprims * gs_num_invocations));
   }

   radeon_set_context_reg(ctx_cs, R_0287FC_GE_MAX_OUTPUT_PER_SUBGROUP,
                          S_0287FC_MAX_VERTS_PER_SUBGROUP(ngg_state->max_out_verts));
   radeon_set_context_reg(ctx_cs, R_028B4C_GE_NGG_SUBGRP_CNTL,
                          S_028B4C_PRIM_AMP_FACTOR(ngg_state->prim_amp_factor) |
                             S_028B4C_THDS_PER_SUBGRP(0)); /* for fast launch */
   radeon_set_context_reg(
      ctx_cs, R_028B90_VGT_GS_INSTANCE_CNT,
      S_028B90_CNT(gs_num_invocations) | S_028B90_ENABLE(gs_num_invocations > 1) |
         S_028B90_EN_MAX_VERT_OUT_PER_GS_INSTANCE(ngg_state->max_vert_out_per_gs_instance));

   if (pdevice->rad_info.gfx_level >= GFX11) {
      ge_cntl = S_03096C_PRIMS_PER_SUBGRP(ngg_state->max_gsprims) |
                S_03096C_VERTS_PER_SUBGRP(ngg_state->enable_vertex_grouping
                                          ? ngg_state->hw_max_esverts
                                          : 256) | /* 256 = disable vertex grouping */
                S_03096C_BREAK_PRIMGRP_AT_EOI(break_wave_at_eoi) |
                S_03096C_PRIM_GRP_SIZE_GFX11(256);
   } else {
      ge_cntl = S_03096C_PRIM_GRP_SIZE_GFX10(ngg_state->max_gsprims) |
                S_03096C_VERT_GRP_SIZE(ngg_state->enable_vertex_grouping
                                          ? ngg_state->hw_max_esverts
                                          : 256) | /* 256 = disable vertex grouping */
                S_03096C_BREAK_WAVE_AT_EOI(break_wave_at_eoi);
   }

   /* Bug workaround for a possible hang with non-tessellation cases.
    * Tessellation always sets GE_CNTL.VERT_GRP_SIZE = 0
    *
    * Requirement: GE_CNTL.VERT_GRP_SIZE = VGT_GS_ONCHIP_CNTL.ES_VERTS_PER_SUBGRP - 5
    */
   if (pdevice->rad_info.gfx_level == GFX10 &&
       !radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL) && ngg_state->hw_max_esverts != 256) {
      ge_cntl &= C_03096C_VERT_GRP_SIZE;

      if (ngg_state->hw_max_esverts > 5) {
         ge_cntl |= S_03096C_VERT_GRP_SIZE(ngg_state->hw_max_esverts - 5);
      }
   }

   radeon_set_uconfig_reg(ctx_cs, R_03096C_GE_CNTL, ge_cntl);

   unsigned late_alloc_wave64, cu_mask;
   ac_compute_late_alloc(&pdevice->rad_info, true, shader->info.has_ngg_culling,
                         shader->config.scratch_bytes_per_wave > 0, &late_alloc_wave64, &cu_mask);

   if (pdevice->rad_info.gfx_level >= GFX11) {
      /* TODO: figure out how S_00B204_CU_EN_GFX11 interacts with ac_set_reg_cu_en */
      gfx10_set_sh_reg_idx3(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
                            S_00B21C_CU_EN(cu_mask) | S_00B21C_WAVE_LIMIT(0x3F));
      gfx10_set_sh_reg_idx3(
         cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS,
         S_00B204_CU_EN_GFX11(0x1) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(late_alloc_wave64));
   } else if (pdevice->rad_info.gfx_level >= GFX10) {
      ac_set_reg_cu_en(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
                       S_00B21C_CU_EN(cu_mask) | S_00B21C_WAVE_LIMIT(0x3F),
                       C_00B21C_CU_EN, 0, &pdevice->rad_info, (void*)gfx10_set_sh_reg_idx3);
      ac_set_reg_cu_en(cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS,
                       S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(late_alloc_wave64),
                       C_00B204_CU_EN_GFX10, 16, &pdevice->rad_info,
                       (void*)gfx10_set_sh_reg_idx3);
   } else {
      radeon_set_sh_reg_idx(
         pdevice, cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS, 3,
         S_00B21C_CU_EN(cu_mask) | S_00B21C_WAVE_LIMIT(0x3F));
      radeon_set_sh_reg_idx(
         pdevice, cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS, 3,
         S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(late_alloc_wave64));
   }

   uint32_t oversub_pc_lines = late_alloc_wave64 ? pdevice->rad_info.pc_lines / 4 : 0;
   if (shader->info.has_ngg_culling) {
      unsigned oversub_factor = 2;

      if (outinfo->param_exports > 4)
         oversub_factor = 4;
      else if (outinfo->param_exports > 2)
         oversub_factor = 3;

      oversub_pc_lines *= oversub_factor;
   }

   gfx10_emit_ge_pc_alloc(cs, pdevice->rad_info.gfx_level, oversub_pc_lines);
}

static void
radv_pipeline_emit_hw_hs(struct radeon_cmdbuf *cs, const struct radv_graphics_pipeline *pipeline,
                         const struct radv_shader *shader)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint64_t va = radv_shader_get_va(shader);

   if (pdevice->rad_info.gfx_level >= GFX9) {
      if (pdevice->rad_info.gfx_level >= GFX10) {
         radeon_set_sh_reg(cs, R_00B520_SPI_SHADER_PGM_LO_LS, va >> 8);
      } else {
         radeon_set_sh_reg(cs, R_00B410_SPI_SHADER_PGM_LO_LS, va >> 8);
      }

      radeon_set_sh_reg_seq(cs, R_00B428_SPI_SHADER_PGM_RSRC1_HS, 2);
      radeon_emit(cs, shader->config.rsrc1);
      radeon_emit(cs, shader->config.rsrc2);
   } else {
      radeon_set_sh_reg_seq(cs, R_00B420_SPI_SHADER_PGM_LO_HS, 4);
      radeon_emit(cs, va >> 8);
      radeon_emit(cs, S_00B424_MEM_BASE(va >> 40));
      radeon_emit(cs, shader->config.rsrc1);
      radeon_emit(cs, shader->config.rsrc2);
   }
}

static void
radv_pipeline_emit_vertex_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                 const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *vs;

   /* Skip shaders merged into HS/GS */
   vs = pipeline->base.shaders[MESA_SHADER_VERTEX];
   if (!vs)
      return;

   if (vs->info.vs.as_ls)
      radv_pipeline_emit_hw_ls(cs, pipeline, vs);
   else if (vs->info.vs.as_es)
      radv_pipeline_emit_hw_es(cs, pipeline, vs);
   else if (vs->info.is_ngg)
      radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, vs);
   else
      radv_pipeline_emit_hw_vs(ctx_cs, cs, pipeline, vs);
}

static void
radv_pipeline_emit_tess_shaders(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *tes, *tcs;

   tcs = pipeline->base.shaders[MESA_SHADER_TESS_CTRL];
   tes = pipeline->base.shaders[MESA_SHADER_TESS_EVAL];

   if (tes) {
      if (tes->info.is_ngg) {
         radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, tes);
      } else if (tes->info.tes.as_es)
         radv_pipeline_emit_hw_es(cs, pipeline, tes);
      else
         radv_pipeline_emit_hw_vs(ctx_cs, cs, pipeline, tes);
   }

   radv_pipeline_emit_hw_hs(cs, pipeline, tcs);

   if (pdevice->rad_info.gfx_level >= GFX10 &&
       !radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && !radv_pipeline_has_ngg(pipeline)) {
      radeon_set_context_reg(ctx_cs, R_028A44_VGT_GS_ONCHIP_CNTL,
                             S_028A44_ES_VERTS_PER_SUBGRP(250) | S_028A44_GS_PRIMS_PER_SUBGRP(126) |
                                S_028A44_GS_INST_PRIMS_IN_SUBGRP(126));
   }
}

static void
radv_pipeline_emit_tess_state(struct radeon_cmdbuf *ctx_cs,
                              const struct radv_graphics_pipeline *pipeline,
                              const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *tes = radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL);
   unsigned type = 0, partitioning = 0, topology = 0, distribution_mode = 0;
   unsigned num_tcs_input_cp, num_tcs_output_cp, num_patches;
   unsigned ls_hs_config;

   num_tcs_input_cp = info->ts.patch_control_points;
   num_tcs_output_cp =
      pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.tcs.tcs_vertices_out; // TCS VERTICES OUT
   num_patches = pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.num_tess_patches;

   ls_hs_config = S_028B58_NUM_PATCHES(num_patches) | S_028B58_HS_NUM_INPUT_CP(num_tcs_input_cp) |
                  S_028B58_HS_NUM_OUTPUT_CP(num_tcs_output_cp);

   if (pdevice->rad_info.gfx_level >= GFX7) {
      radeon_set_context_reg_idx(ctx_cs, R_028B58_VGT_LS_HS_CONFIG, 2, ls_hs_config);
   } else {
      radeon_set_context_reg(ctx_cs, R_028B58_VGT_LS_HS_CONFIG, ls_hs_config);
   }

   switch (tes->info.tes._primitive_mode) {
   case TESS_PRIMITIVE_TRIANGLES:
      type = V_028B6C_TESS_TRIANGLE;
      break;
   case TESS_PRIMITIVE_QUADS:
      type = V_028B6C_TESS_QUAD;
      break;
   case TESS_PRIMITIVE_ISOLINES:
      type = V_028B6C_TESS_ISOLINE;
      break;
   default:
      break;
   }

   switch (tes->info.tes.spacing) {
   case TESS_SPACING_EQUAL:
      partitioning = V_028B6C_PART_INTEGER;
      break;
   case TESS_SPACING_FRACTIONAL_ODD:
      partitioning = V_028B6C_PART_FRAC_ODD;
      break;
   case TESS_SPACING_FRACTIONAL_EVEN:
      partitioning = V_028B6C_PART_FRAC_EVEN;
      break;
   default:
      break;
   }

   bool ccw = tes->info.tes.ccw;
   if (info->ts.domain_origin != VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT)
      ccw = !ccw;

   if (tes->info.tes.point_mode)
      topology = V_028B6C_OUTPUT_POINT;
   else if (tes->info.tes._primitive_mode == TESS_PRIMITIVE_ISOLINES)
      topology = V_028B6C_OUTPUT_LINE;
   else if (ccw)
      topology = V_028B6C_OUTPUT_TRIANGLE_CCW;
   else
      topology = V_028B6C_OUTPUT_TRIANGLE_CW;

   if (pdevice->rad_info.has_distributed_tess) {
      if (pdevice->rad_info.family == CHIP_FIJI || pdevice->rad_info.family >= CHIP_POLARIS10)
         distribution_mode = V_028B6C_TRAPEZOIDS;
      else
         distribution_mode = V_028B6C_DONUTS;
   } else
      distribution_mode = V_028B6C_NO_DIST;

   radeon_set_context_reg(ctx_cs, R_028B6C_VGT_TF_PARAM,
                          S_028B6C_TYPE(type) | S_028B6C_PARTITIONING(partitioning) |
                             S_028B6C_TOPOLOGY(topology) |
                             S_028B6C_DISTRIBUTION_MODE(distribution_mode));
}

static void
radv_pipeline_emit_hw_gs(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                         const struct radv_graphics_pipeline *pipeline, const struct radv_shader *gs)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct gfx9_gs_info *gs_state = &gs->info.gs_ring_info;
   unsigned gs_max_out_vertices;
   const uint8_t *num_components;
   uint8_t max_stream;
   unsigned offset;
   uint64_t va;

   gs_max_out_vertices = gs->info.gs.vertices_out;
   max_stream = gs->info.gs.max_stream;
   num_components = gs->info.gs.num_stream_output_components;

   offset = num_components[0] * gs_max_out_vertices;

   radeon_set_context_reg_seq(ctx_cs, R_028A60_VGT_GSVS_RING_OFFSET_1, 3);
   radeon_emit(ctx_cs, offset);
   if (max_stream >= 1)
      offset += num_components[1] * gs_max_out_vertices;
   radeon_emit(ctx_cs, offset);
   if (max_stream >= 2)
      offset += num_components[2] * gs_max_out_vertices;
   radeon_emit(ctx_cs, offset);
   if (max_stream >= 3)
      offset += num_components[3] * gs_max_out_vertices;
   radeon_set_context_reg(ctx_cs, R_028AB0_VGT_GSVS_RING_ITEMSIZE, offset);

   radeon_set_context_reg_seq(ctx_cs, R_028B5C_VGT_GS_VERT_ITEMSIZE, 4);
   radeon_emit(ctx_cs, num_components[0]);
   radeon_emit(ctx_cs, (max_stream >= 1) ? num_components[1] : 0);
   radeon_emit(ctx_cs, (max_stream >= 2) ? num_components[2] : 0);
   radeon_emit(ctx_cs, (max_stream >= 3) ? num_components[3] : 0);

   uint32_t gs_num_invocations = gs->info.gs.invocations;
   radeon_set_context_reg(
      ctx_cs, R_028B90_VGT_GS_INSTANCE_CNT,
      S_028B90_CNT(MIN2(gs_num_invocations, 127)) | S_028B90_ENABLE(gs_num_invocations > 0));

   radeon_set_context_reg(ctx_cs, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
                          gs_state->vgt_esgs_ring_itemsize);

   va = radv_shader_get_va(gs);

   if (pdevice->rad_info.gfx_level >= GFX9) {
      if (pdevice->rad_info.gfx_level >= GFX10) {
         radeon_set_sh_reg(cs, R_00B320_SPI_SHADER_PGM_LO_ES, va >> 8);
      } else {
         radeon_set_sh_reg(cs, R_00B210_SPI_SHADER_PGM_LO_ES, va >> 8);
      }

      radeon_set_sh_reg_seq(cs, R_00B228_SPI_SHADER_PGM_RSRC1_GS, 2);
      radeon_emit(cs, gs->config.rsrc1);
      radeon_emit(cs, gs->config.rsrc2 | S_00B22C_LDS_SIZE(gs_state->lds_size));

      radeon_set_context_reg(ctx_cs, R_028A44_VGT_GS_ONCHIP_CNTL, gs_state->vgt_gs_onchip_cntl);
      radeon_set_context_reg(ctx_cs, R_028A94_VGT_GS_MAX_PRIMS_PER_SUBGROUP,
                             gs_state->vgt_gs_max_prims_per_subgroup);
   } else {
      radeon_set_sh_reg_seq(cs, R_00B220_SPI_SHADER_PGM_LO_GS, 4);
      radeon_emit(cs, va >> 8);
      radeon_emit(cs, S_00B224_MEM_BASE(va >> 40));
      radeon_emit(cs, gs->config.rsrc1);
      radeon_emit(cs, gs->config.rsrc2);
   }

   if (pdevice->rad_info.gfx_level >= GFX10) {
      ac_set_reg_cu_en(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
                       S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3F),
                       C_00B21C_CU_EN, 0, &pdevice->rad_info,
                       (void*)gfx10_set_sh_reg_idx3);
      ac_set_reg_cu_en(cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS,
                       S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(0),
                       C_00B204_CU_EN_GFX10, 16, &pdevice->rad_info,
                       (void*)gfx10_set_sh_reg_idx3);
   } else if (pdevice->rad_info.gfx_level >= GFX7) {
      radeon_set_sh_reg_idx(
         pdevice, cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS, 3,
         S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3F));

      if (pdevice->rad_info.gfx_level >= GFX10) {
         radeon_set_sh_reg_idx(
            pdevice, cs, R_00B204_SPI_SHADER_PGM_RSRC4_GS, 3,
            S_00B204_CU_EN_GFX10(0xffff) | S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(0));
      }
   }

   radv_pipeline_emit_hw_vs(ctx_cs, cs, pipeline, pipeline->base.gs_copy_shader);
}

static void
radv_pipeline_emit_geometry_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                   const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *gs;

   gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];
   if (!gs)
      return;

   if (gs->info.is_ngg)
      radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, gs);
   else
      radv_pipeline_emit_hw_gs(ctx_cs, cs, pipeline, gs);

   radeon_set_context_reg(ctx_cs, R_028B38_VGT_GS_MAX_VERT_OUT, gs->info.gs.vertices_out);
}

static void
radv_pipeline_emit_mesh_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                               const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *ms = pipeline->base.shaders[MESA_SHADER_MESH];
   if (!ms)
      return;

   radv_pipeline_emit_hw_ngg(ctx_cs, cs, pipeline, ms);
   radeon_set_context_reg(ctx_cs, R_028B38_VGT_GS_MAX_VERT_OUT, ms->info.workgroup_size);
   radeon_set_uconfig_reg_idx(pdevice, ctx_cs,
                              R_030908_VGT_PRIMITIVE_TYPE, 1, V_008958_DI_PT_POINTLIST);
}

static uint32_t
offset_to_ps_input(uint32_t offset, bool flat_shade, bool explicit, bool float16)
{
   uint32_t ps_input_cntl;
   if (offset <= AC_EXP_PARAM_OFFSET_31) {
      ps_input_cntl = S_028644_OFFSET(offset);
      if (flat_shade || explicit)
         ps_input_cntl |= S_028644_FLAT_SHADE(1);
      if (explicit) {
         /* Force parameter cache to be read in passthrough
          * mode.
          */
         ps_input_cntl |= S_028644_OFFSET(1 << 5);
      }
      if (float16) {
         ps_input_cntl |= S_028644_FP16_INTERP_MODE(1) | S_028644_ATTR0_VALID(1);
      }
   } else {
      /* The input is a DEFAULT_VAL constant. */
      assert(offset >= AC_EXP_PARAM_DEFAULT_VAL_0000 && offset <= AC_EXP_PARAM_DEFAULT_VAL_1111);
      offset -= AC_EXP_PARAM_DEFAULT_VAL_0000;
      ps_input_cntl = S_028644_OFFSET(0x20) | S_028644_DEFAULT_VAL(offset);
   }
   return ps_input_cntl;
}

static void
single_slot_to_ps_input(const struct radv_vs_output_info *outinfo,
                        unsigned slot, uint32_t *ps_input_cntl, unsigned *ps_offset,
                        bool skip_undef, bool use_default_0, bool flat_shade)
{
   unsigned vs_offset = outinfo->vs_output_param_offset[slot];

   if (vs_offset == AC_EXP_PARAM_UNDEFINED) {
      if (skip_undef)
         return;
      else if (use_default_0)
         vs_offset = AC_EXP_PARAM_DEFAULT_VAL_0000;
      else
         unreachable("vs_offset should not be AC_EXP_PARAM_UNDEFINED.");
   }

   ps_input_cntl[*ps_offset] = offset_to_ps_input(vs_offset, flat_shade, false, false);
   ++(*ps_offset);
}

static void
input_mask_to_ps_inputs(const struct radv_vs_output_info *outinfo, const struct radv_shader *ps,
                        uint32_t input_mask, uint32_t *ps_input_cntl, unsigned *ps_offset)
{
   u_foreach_bit(i, input_mask) {
      unsigned vs_offset = outinfo->vs_output_param_offset[VARYING_SLOT_VAR0 + i];
      if (vs_offset == AC_EXP_PARAM_UNDEFINED) {
         ps_input_cntl[*ps_offset] = S_028644_OFFSET(0x20);
         ++(*ps_offset);
         continue;
      }

      bool flat_shade = !!(ps->info.ps.flat_shaded_mask & (1u << *ps_offset));
      bool explicit = !!(ps->info.ps.explicit_shaded_mask & (1u << *ps_offset));
      bool float16 = !!(ps->info.ps.float16_shaded_mask & (1u << *ps_offset));

      ps_input_cntl[*ps_offset] = offset_to_ps_input(vs_offset, flat_shade, explicit, float16);
      ++(*ps_offset);
   }
}

static void
radv_pipeline_emit_ps_inputs(struct radeon_cmdbuf *ctx_cs,
                             const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);
   bool mesh = radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH);
   uint32_t ps_input_cntl[32];

   unsigned ps_offset = 0;

   if (ps->info.ps.prim_id_input && !mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_PRIMITIVE_ID, ps_input_cntl, &ps_offset,
                              true, false, true);

   if (ps->info.ps.layer_input && !mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_LAYER, ps_input_cntl, &ps_offset,
                              false, true, true);

   if (ps->info.ps.viewport_index_input && !mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_VIEWPORT, ps_input_cntl, &ps_offset,
                              false, false, true);

   if (ps->info.ps.has_pcoord)
      ps_input_cntl[ps_offset++] = S_028644_PT_SPRITE_TEX(1) | S_028644_OFFSET(0x20);

   if (ps->info.ps.num_input_clips_culls) {
      single_slot_to_ps_input(outinfo, VARYING_SLOT_CLIP_DIST0, ps_input_cntl, &ps_offset,
                              true, false, false);

      if (ps->info.ps.num_input_clips_culls > 4)
         single_slot_to_ps_input(outinfo, VARYING_SLOT_CLIP_DIST1, ps_input_cntl, &ps_offset,
                                 true, false, false);
   }

   input_mask_to_ps_inputs(outinfo, ps, ps->info.ps.input_mask,
                           ps_input_cntl, &ps_offset);

   /* Per-primitive PS inputs: the HW needs these to be last. */

   if (ps->info.ps.prim_id_input && mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_PRIMITIVE_ID, ps_input_cntl, &ps_offset,
                              true, false, false);

   if (ps->info.ps.layer_input && mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_LAYER, ps_input_cntl, &ps_offset,
                              false, true, false);

   if (ps->info.ps.viewport_index_input && mesh)
      single_slot_to_ps_input(outinfo, VARYING_SLOT_VIEWPORT, ps_input_cntl, &ps_offset,
                              false, false, false);

   input_mask_to_ps_inputs(outinfo, ps, ps->info.ps.input_per_primitive_mask,
                           ps_input_cntl, &ps_offset);

   if (ps_offset) {
      radeon_set_context_reg_seq(ctx_cs, R_028644_SPI_PS_INPUT_CNTL_0, ps_offset);
      for (unsigned i = 0; i < ps_offset; i++) {
         radeon_emit(ctx_cs, ps_input_cntl[i]);
      }
   }
}

static uint32_t
radv_compute_db_shader_control(const struct radv_physical_device *pdevice,
                               const struct radv_graphics_pipeline *pipeline,
                               const struct radv_shader *ps)
{
   unsigned conservative_z_export = V_02880C_EXPORT_ANY_Z;
   unsigned z_order;
   if (ps->info.ps.early_fragment_test || !ps->info.ps.writes_memory)
      z_order = V_02880C_EARLY_Z_THEN_LATE_Z;
   else
      z_order = V_02880C_LATE_Z;

   if (ps->info.ps.depth_layout == FRAG_DEPTH_LAYOUT_GREATER)
      conservative_z_export = V_02880C_EXPORT_GREATER_THAN_Z;
   else if (ps->info.ps.depth_layout == FRAG_DEPTH_LAYOUT_LESS)
      conservative_z_export = V_02880C_EXPORT_LESS_THAN_Z;

   bool disable_rbplus = pdevice->rad_info.has_rbplus && !pdevice->rad_info.rbplus_allowed;

   /* It shouldn't be needed to export gl_SampleMask when MSAA is disabled
    * but this appears to break Project Cars (DXVK). See
    * https://bugs.freedesktop.org/show_bug.cgi?id=109401
    */
   bool mask_export_enable = ps->info.ps.writes_sample_mask;

   return S_02880C_Z_EXPORT_ENABLE(ps->info.ps.writes_z) |
          S_02880C_STENCIL_TEST_VAL_EXPORT_ENABLE(ps->info.ps.writes_stencil) |
          S_02880C_KILL_ENABLE(!!ps->info.ps.can_discard) |
          S_02880C_MASK_EXPORT_ENABLE(mask_export_enable) |
          S_02880C_CONSERVATIVE_Z_EXPORT(conservative_z_export) | S_02880C_Z_ORDER(z_order) |
          S_02880C_DEPTH_BEFORE_SHADER(ps->info.ps.early_fragment_test) |
          S_02880C_PRE_SHADER_DEPTH_COVERAGE_ENABLE(ps->info.ps.post_depth_coverage) |
          S_02880C_EXEC_ON_HIER_FAIL(ps->info.ps.writes_memory) |
          S_02880C_EXEC_ON_NOOP(ps->info.ps.writes_memory) |
          S_02880C_DUAL_QUAD_DISABLE(disable_rbplus);
}

static void
radv_pipeline_emit_fragment_shader(struct radeon_cmdbuf *ctx_cs, struct radeon_cmdbuf *cs,
                                   const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *ps;
   bool param_gen;
   uint64_t va;
   assert(pipeline->base.shaders[MESA_SHADER_FRAGMENT]);

   ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   va = radv_shader_get_va(ps);

   radeon_set_sh_reg_seq(cs, R_00B020_SPI_SHADER_PGM_LO_PS, 4);
   radeon_emit(cs, va >> 8);
   radeon_emit(cs, S_00B024_MEM_BASE(va >> 40));
   radeon_emit(cs, ps->config.rsrc1);
   radeon_emit(cs, ps->config.rsrc2);

   radeon_set_context_reg(ctx_cs, R_02880C_DB_SHADER_CONTROL,
                          radv_compute_db_shader_control(pdevice, pipeline, ps));

   radeon_set_context_reg_seq(ctx_cs, R_0286CC_SPI_PS_INPUT_ENA, 2);
   radeon_emit(ctx_cs, ps->config.spi_ps_input_ena);
   radeon_emit(ctx_cs, ps->config.spi_ps_input_addr);

   /* Workaround when there are no PS inputs but LDS is used. */
   param_gen = pdevice->rad_info.gfx_level >= GFX11 &&
               !ps->info.ps.num_interp && ps->config.lds_size;

   radeon_set_context_reg(
      ctx_cs, R_0286D8_SPI_PS_IN_CONTROL,
      S_0286D8_NUM_INTERP(ps->info.ps.num_interp) |
      S_0286D8_NUM_PRIM_INTERP(ps->info.ps.num_prim_interp) |
      S_0286D8_PS_W32_EN(ps->info.wave_size == 32) |
      S_0286D8_PARAM_GEN(param_gen));

   radeon_set_context_reg(ctx_cs, R_0286E0_SPI_BARYC_CNTL, pipeline->spi_baryc_cntl);

   radeon_set_context_reg(
      ctx_cs, R_028710_SPI_SHADER_Z_FORMAT,
      ac_get_spi_shader_z_format(ps->info.ps.writes_z, ps->info.ps.writes_stencil,
                                 ps->info.ps.writes_sample_mask, false));
}

static void
radv_pipeline_emit_vgt_vertex_reuse(struct radeon_cmdbuf *ctx_cs,
                                    const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;

   if (pdevice->rad_info.family < CHIP_POLARIS10 || pdevice->rad_info.gfx_level >= GFX10)
      return;

   unsigned vtx_reuse_depth = 30;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL) &&
       radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL)->info.tes.spacing ==
          TESS_SPACING_FRACTIONAL_ODD) {
      vtx_reuse_depth = 14;
   }
   radeon_set_context_reg(ctx_cs, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL,
                          S_028C58_VTX_REUSE_DEPTH(vtx_reuse_depth));
}

static void
radv_pipeline_emit_vgt_shader_config(struct radeon_cmdbuf *ctx_cs,
                                     const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint32_t stages = 0;
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      stages |= S_028B54_LS_EN(V_028B54_LS_STAGE_ON) | S_028B54_HS_EN(1) | S_028B54_DYNAMIC_HS(1);

      if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY))
         stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_DS) | S_028B54_GS_EN(1);
      else if (radv_pipeline_has_ngg(pipeline))
         stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_DS);
      else
         stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_DS);
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL) | S_028B54_GS_EN(1);
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      assert(!radv_pipeline_has_ngg_passthrough(pipeline));
      stages |= S_028B54_GS_EN(1) | S_028B54_GS_FAST_LAUNCH(1);

      if (pipeline->base.shaders[MESA_SHADER_MESH]->info.ms.needs_ms_scratch_ring)
         stages |= S_028B54_NGG_WAVE_ID_EN(1);
   } else if (radv_pipeline_has_ngg(pipeline)) {
      stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL);
   }

   if (radv_pipeline_has_ngg(pipeline)) {
      stages |= S_028B54_PRIMGEN_EN(1);
      if (pipeline->streamout_shader)
         stages |= S_028B54_NGG_WAVE_ID_EN(1);
      if (radv_pipeline_has_ngg_passthrough(pipeline)) {
         stages |= S_028B54_PRIMGEN_PASSTHRU_EN(1);
         if (pdevice->rad_info.family >= CHIP_NAVI23)
            stages |= S_028B54_PRIMGEN_PASSTHRU_NO_MSG(1);
      }
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
   }

   if (pdevice->rad_info.gfx_level >= GFX9)
      stages |= S_028B54_MAX_PRIMGRP_IN_WAVE(2);

   if (pdevice->rad_info.gfx_level >= GFX10) {
      uint8_t hs_size = 64, gs_size = 64, vs_size = 64;

      if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL))
         hs_size = pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.wave_size;

      if (pipeline->base.shaders[MESA_SHADER_GEOMETRY]) {
         vs_size = gs_size = pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.wave_size;
         if (radv_pipeline_has_gs_copy_shader(&pipeline->base))
            vs_size = pipeline->base.gs_copy_shader->info.wave_size;
      } else if (pipeline->base.shaders[MESA_SHADER_TESS_EVAL])
         vs_size = pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.wave_size;
      else if (pipeline->base.shaders[MESA_SHADER_VERTEX])
         vs_size = pipeline->base.shaders[MESA_SHADER_VERTEX]->info.wave_size;
      else if (pipeline->base.shaders[MESA_SHADER_MESH])
         vs_size = gs_size = pipeline->base.shaders[MESA_SHADER_MESH]->info.wave_size;

      if (radv_pipeline_has_ngg(pipeline)) {
         assert(!radv_pipeline_has_gs_copy_shader(&pipeline->base));
         gs_size = vs_size;
      }

      /* legacy GS only supports Wave64 */
      stages |= S_028B54_HS_W32_EN(hs_size == 32 ? 1 : 0) |
                S_028B54_GS_W32_EN(gs_size == 32 ? 1 : 0) |
                S_028B54_VS_W32_EN(vs_size == 32 ? 1 : 0);
   }

   radeon_set_context_reg(ctx_cs, R_028B54_VGT_SHADER_STAGES_EN, stages);
}

static void
radv_pipeline_emit_cliprect_rule(struct radeon_cmdbuf *ctx_cs,
                                 const struct radv_graphics_pipeline_info *info)
{
   uint32_t cliprect_rule = 0;

   if (!info->dr.count) {
      cliprect_rule = 0xffff;
   } else {
      for (unsigned i = 0; i < (1u << MAX_DISCARD_RECTANGLES); ++i) {
         /* Interpret i as a bitmask, and then set the bit in
          * the mask if that combination of rectangles in which
          * the pixel is contained should pass the cliprect
          * test.
          */
         unsigned relevant_subset = i & ((1u << info->dr.count) - 1);

         if (info->dr.mode == VK_DISCARD_RECTANGLE_MODE_INCLUSIVE_EXT && !relevant_subset)
            continue;

         if (info->dr.mode == VK_DISCARD_RECTANGLE_MODE_EXCLUSIVE_EXT && relevant_subset)
            continue;

         cliprect_rule |= 1u << i;
      }
   }

   radeon_set_context_reg(ctx_cs, R_02820C_PA_SC_CLIPRECT_RULE, cliprect_rule);
}

static void
gfx10_pipeline_emit_ge_cntl(struct radeon_cmdbuf *ctx_cs,
                            const struct radv_graphics_pipeline *pipeline)
{
   bool break_wave_at_eoi = false;
   unsigned primgroup_size;
   unsigned vertgroup_size = 256; /* 256 = disable vertex grouping */

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      primgroup_size = pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.num_tess_patches;
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      const struct gfx9_gs_info *gs_state =
         &pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.gs_ring_info;
      unsigned vgt_gs_onchip_cntl = gs_state->vgt_gs_onchip_cntl;
      primgroup_size = G_028A44_GS_PRIMS_PER_SUBGRP(vgt_gs_onchip_cntl);
   } else {
      primgroup_size = 128; /* recommended without a GS and tess */
   }

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      if (pipeline->base.shaders[MESA_SHADER_TESS_CTRL]->info.uses_prim_id ||
          radv_get_shader(&pipeline->base, MESA_SHADER_TESS_EVAL)->info.uses_prim_id)
         break_wave_at_eoi = true;
   }

   radeon_set_uconfig_reg(ctx_cs, R_03096C_GE_CNTL,
                          S_03096C_PRIM_GRP_SIZE_GFX10(primgroup_size) |
                             S_03096C_VERT_GRP_SIZE(vertgroup_size) |
                             S_03096C_PACKET_TO_ONE_PA(0) /* line stipple */ |
                             S_03096C_BREAK_WAVE_AT_EOI(break_wave_at_eoi));
}

static void
radv_pipeline_emit_vgt_gs_out(struct radeon_cmdbuf *ctx_cs,
                              const struct radv_graphics_pipeline *pipeline,
                              uint32_t vgt_gs_out_prim_type)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;

   if (pdevice->rad_info.gfx_level >= GFX11) {
      radeon_set_uconfig_reg(ctx_cs, R_030998_VGT_GS_OUT_PRIM_TYPE, vgt_gs_out_prim_type);
   } else {
      radeon_set_context_reg(ctx_cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE, vgt_gs_out_prim_type);
   }
}

static void
gfx103_pipeline_emit_vgt_draw_payload_cntl(struct radeon_cmdbuf *ctx_cs,
                                           const struct radv_graphics_pipeline *pipeline,
                                           const struct radv_graphics_pipeline_info *info)
{
   const struct radv_vs_output_info *outinfo = get_vs_output_info(pipeline);

   bool enable_vrs = radv_is_vrs_enabled(pipeline, info);

   /* Enables the second channel of the primitive export instruction.
    * This channel contains: VRS rate x, y, viewport and layer.
    */
   bool enable_prim_payload =
      outinfo &&
      (outinfo->writes_viewport_index_per_primitive ||
       outinfo->writes_layer_per_primitive ||
       outinfo->writes_primitive_shading_rate_per_primitive);

   radeon_set_context_reg(ctx_cs, R_028A98_VGT_DRAW_PAYLOAD_CNTL,
                          S_028A98_EN_VRS_RATE(enable_vrs) |
                          S_028A98_EN_PRIM_PAYLOAD(enable_prim_payload));
}

static bool
gfx103_pipeline_vrs_coarse_shading(const struct radv_graphics_pipeline *pipeline)
{
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   struct radv_device *device = pipeline->base.device;

   if (device->instance->debug_flags & RADV_DEBUG_NO_VRS_FLAT_SHADING)
      return false;

   if (!ps->info.ps.allow_flat_shading)
      return false;

   return true;
}

static void
gfx103_pipeline_emit_vrs_state(struct radeon_cmdbuf *ctx_cs,
                               const struct radv_graphics_pipeline *pipeline,
                               const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   uint32_t mode = V_028064_VRS_COMB_MODE_PASSTHRU;
   uint8_t rate_x = 0, rate_y = 0;
   bool enable_vrs = radv_is_vrs_enabled(pipeline, info);

   if (!enable_vrs && gfx103_pipeline_vrs_coarse_shading(pipeline)) {
      /* When per-draw VRS is not enabled at all, try enabling VRS coarse shading 2x2 if the driver
       * determined that it's safe to enable.
       */
      mode = V_028064_VRS_COMB_MODE_OVERRIDE;
      rate_x = rate_y = 1;
   } else if (!radv_is_static_vrs_enabled(pipeline, info) && pipeline->force_vrs_per_vertex &&
              get_vs_output_info(pipeline)->writes_primitive_shading_rate) {
      /* Otherwise, if per-draw VRS is not enabled statically, try forcing per-vertex VRS if
       * requested by the user. Note that vkd3d-proton always has to declare VRS as dynamic because
       * in DX12 it's fully dynamic.
       */
      radeon_set_context_reg(ctx_cs, R_028848_PA_CL_VRS_CNTL,
         S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE) |
         S_028848_VERTEX_RATE_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE));

      /* If the shader is using discard, turn off coarse shading because discard at 2x2 pixel
       * granularity degrades quality too much. MIN allows sample shading but not coarse shading.
       */
      struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];

      mode = ps->info.ps.can_discard ? V_028064_VRS_COMB_MODE_MIN : V_028064_VRS_COMB_MODE_PASSTHRU;
   }

   if (pdevice->rad_info.gfx_level >= GFX11) {
      radeon_set_context_reg(ctx_cs, R_0283D0_PA_SC_VRS_OVERRIDE_CNTL,
                             S_0283D0_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                                S_0283D0_VRS_RATE((rate_x << 2) | rate_y));
   } else {
      radeon_set_context_reg(ctx_cs, R_028064_DB_VRS_OVERRIDE_CNTL,
                             S_028064_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                                S_028064_VRS_OVERRIDE_RATE_X(rate_x) |
                                S_028064_VRS_OVERRIDE_RATE_Y(rate_y));
   }
}

static void
radv_pipeline_emit_pm4(struct radv_graphics_pipeline *pipeline,
                       const struct radv_blend_state *blend,
                       const struct radv_depth_stencil_state *ds_state,
                       uint32_t vgt_gs_out_prim_type,
                       const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radeon_cmdbuf *ctx_cs = &pipeline->base.ctx_cs;
   struct radeon_cmdbuf *cs = &pipeline->base.cs;

   cs->max_dw = 64;
   ctx_cs->max_dw = 256;
   cs->buf = malloc(4 * (cs->max_dw + ctx_cs->max_dw));
   ctx_cs->buf = cs->buf + cs->max_dw;

   radv_pipeline_emit_depth_stencil_state(ctx_cs, ds_state);
   radv_pipeline_emit_blend_state(ctx_cs, pipeline, blend);
   radv_pipeline_emit_raster_state(ctx_cs, pipeline, info);
   radv_pipeline_emit_multisample_state(ctx_cs, pipeline);
   radv_pipeline_emit_vgt_gs_mode(ctx_cs, pipeline);
   radv_pipeline_emit_vertex_shader(ctx_cs, cs, pipeline);
   radv_pipeline_emit_mesh_shader(ctx_cs, cs, pipeline);

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      radv_pipeline_emit_tess_shaders(ctx_cs, cs, pipeline);
      radv_pipeline_emit_tess_state(ctx_cs, pipeline, info);
   }

   radv_pipeline_emit_geometry_shader(ctx_cs, cs, pipeline);
   radv_pipeline_emit_fragment_shader(ctx_cs, cs, pipeline);
   radv_pipeline_emit_ps_inputs(ctx_cs, pipeline);
   radv_pipeline_emit_vgt_vertex_reuse(ctx_cs, pipeline);
   radv_pipeline_emit_vgt_shader_config(ctx_cs, pipeline);
   radv_pipeline_emit_cliprect_rule(ctx_cs, info);
   radv_pipeline_emit_vgt_gs_out(ctx_cs, pipeline, vgt_gs_out_prim_type);

   if (pdevice->rad_info.gfx_level >= GFX10 && !radv_pipeline_has_ngg(pipeline))
      gfx10_pipeline_emit_ge_cntl(ctx_cs, pipeline);

   if (pdevice->rad_info.gfx_level >= GFX10_3) {
      gfx103_pipeline_emit_vgt_draw_payload_cntl(ctx_cs, pipeline, info);
      gfx103_pipeline_emit_vrs_state(ctx_cs, pipeline, info);
   }

   pipeline->base.ctx_cs_hash = _mesa_hash_data(ctx_cs->buf, ctx_cs->cdw * 4);

   assert(ctx_cs->cdw <= ctx_cs->max_dw);
   assert(cs->cdw <= cs->max_dw);
}

static void
radv_pipeline_init_vertex_input_state(struct radv_graphics_pipeline *pipeline,
                                      const struct radv_graphics_pipeline_info *info)
{
   const struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   const struct radv_shader_info *vs_info = &radv_get_shader(&pipeline->base, MESA_SHADER_VERTEX)->info;

   for (uint32_t i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
      pipeline->attrib_ends[i] = info->vi.attrib_ends[i];
      pipeline->attrib_index_offset[i] = info->vi.attrib_index_offset[i];
      pipeline->attrib_bindings[i] = info->vi.attrib_bindings[i];
   }

   for (uint32_t i = 0; i < MAX_VBS; i++) {
      pipeline->binding_stride[i] = info->vi.binding_stride[i];
   }

   pipeline->use_per_attribute_vb_descs = vs_info->vs.use_per_attribute_vb_descs;
   pipeline->last_vertex_attrib_bit = util_last_bit(vs_info->vs.vb_desc_usage_mask);
   if (pipeline->base.shaders[MESA_SHADER_VERTEX])
      pipeline->next_vertex_stage = MESA_SHADER_VERTEX;
   else if (pipeline->base.shaders[MESA_SHADER_TESS_CTRL])
      pipeline->next_vertex_stage = MESA_SHADER_TESS_CTRL;
   else
      pipeline->next_vertex_stage = MESA_SHADER_GEOMETRY;
   if (pipeline->next_vertex_stage == MESA_SHADER_VERTEX) {
      const struct radv_shader *vs_shader = pipeline->base.shaders[MESA_SHADER_VERTEX];
      pipeline->can_use_simple_input = vs_shader->info.is_ngg == pdevice->use_ngg &&
                                       vs_shader->info.wave_size == pdevice->ge_wave_size;
   } else {
      pipeline->can_use_simple_input = false;
   }
   if (vs_info->vs.dynamic_inputs)
      pipeline->vb_desc_usage_mask = BITFIELD_MASK(pipeline->last_vertex_attrib_bit);
   else
      pipeline->vb_desc_usage_mask = vs_info->vs.vb_desc_usage_mask;
   pipeline->vb_desc_alloc_size = util_bitcount(pipeline->vb_desc_usage_mask) * 16;
}

static struct radv_shader *
radv_pipeline_get_streamout_shader(struct radv_graphics_pipeline *pipeline)
{
   int i;

   for (i = MESA_SHADER_GEOMETRY; i >= MESA_SHADER_VERTEX; i--) {
      struct radv_shader *shader = radv_get_shader(&pipeline->base, i);

      if (shader && shader->info.so.num_outputs > 0)
         return shader;
   }

   return NULL;
}

static bool
radv_shader_need_indirect_descriptor_sets(struct radv_pipeline *pipeline, gl_shader_stage stage)
{
   struct radv_userdata_info *loc =
      radv_lookup_user_sgpr(pipeline, stage, AC_UD_INDIRECT_DESCRIPTOR_SETS);
   return loc->sgpr_idx != -1;
}

static void
radv_pipeline_init_shader_stages_state(struct radv_graphics_pipeline *pipeline)
{
   struct radv_device *device = pipeline->base.device;

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      bool shader_exists = !!pipeline->base.shaders[i];
      if (shader_exists || i < MESA_SHADER_COMPUTE) {
         /* We need this info for some stages even when the shader doesn't exist. */
         pipeline->base.user_data_0[i] = radv_pipeline_stage_to_user_data_0(
            pipeline, i, device->physical_device->rad_info.gfx_level);

         if (shader_exists)
            pipeline->base.need_indirect_descriptor_sets |=
               radv_shader_need_indirect_descriptor_sets(&pipeline->base, i);
      }
   }

   gl_shader_stage first_stage =
      radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH) ? MESA_SHADER_MESH : MESA_SHADER_VERTEX;

   struct radv_userdata_info *loc =
      radv_lookup_user_sgpr(&pipeline->base, first_stage, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   if (loc->sgpr_idx != -1) {
      pipeline->vtx_base_sgpr = pipeline->base.user_data_0[first_stage];
      pipeline->vtx_base_sgpr += loc->sgpr_idx * 4;
      pipeline->vtx_emit_num = loc->num_sgprs;
      pipeline->uses_drawid =
         radv_get_shader(&pipeline->base, first_stage)->info.vs.needs_draw_id;
      pipeline->uses_baseinstance =
         radv_get_shader(&pipeline->base, first_stage)->info.vs.needs_base_instance;

      assert(first_stage != MESA_SHADER_MESH || !pipeline->uses_baseinstance);
   }
}

static uint32_t
radv_pipeline_init_vgt_gs_out(struct radv_graphics_pipeline *pipeline,
                              const struct radv_graphics_pipeline_info *info)
{
   uint32_t gs_out;

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      gs_out =
         si_conv_gl_prim_to_gs_out(pipeline->base.shaders[MESA_SHADER_GEOMETRY]->info.gs.output_prim);
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      if (pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.tes.point_mode) {
         gs_out = V_028A6C_POINTLIST;
      } else {
         gs_out = si_conv_tess_prim_to_gs_out(
            pipeline->base.shaders[MESA_SHADER_TESS_EVAL]->info.tes._primitive_mode);
      }
   } else if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      gs_out =
         si_conv_gl_prim_to_gs_out(pipeline->base.shaders[MESA_SHADER_MESH]->info.ms.output_prim);
   } else {
      gs_out = si_conv_prim_to_gs_out(info->ia.primitive_topology);
   }

   return gs_out;
}

static void
radv_pipeline_init_extra(struct radv_graphics_pipeline *pipeline,
                         const struct radv_graphics_pipeline_create_info *extra,
                         struct radv_blend_state *blend_state,
                         struct radv_depth_stencil_state *ds_state,
                         const struct radv_graphics_pipeline_info *info,
                         uint32_t *vgt_gs_out_prim_type)
{
   if (extra->custom_blend_mode == V_028808_CB_ELIMINATE_FAST_CLEAR ||
       extra->custom_blend_mode == V_028808_CB_FMASK_DECOMPRESS ||
       extra->custom_blend_mode == V_028808_CB_DCC_DECOMPRESS_GFX8 ||
       extra->custom_blend_mode == V_028808_CB_DCC_DECOMPRESS_GFX11 ||
       extra->custom_blend_mode == V_028808_CB_RESOLVE) {
      /* According to the CB spec states, CB_SHADER_MASK should be set to enable writes to all four
       * channels of MRT0.
       */
      blend_state->cb_shader_mask = 0xf;

      if (extra->custom_blend_mode == V_028808_CB_RESOLVE)
         pipeline->cb_color_control |= S_028808_DISABLE_DUAL_QUAD(1);

      pipeline->cb_color_control &= C_028808_MODE;
      pipeline->cb_color_control |= S_028808_MODE(extra->custom_blend_mode);
   }

   if (extra->use_rectlist) {
      struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;
      dynamic->primitive_topology = V_008958_DI_PT_RECTLIST;

      *vgt_gs_out_prim_type = V_028A6C_TRISTRIP;
      if (radv_pipeline_has_ngg(pipeline))
         *vgt_gs_out_prim_type = V_028A6C_RECTLIST;

      pipeline->rast_prim = *vgt_gs_out_prim_type;
   }

   if (radv_pipeline_has_ds_attachments(&info->ri)) {
      ds_state->db_render_control |= S_028000_DEPTH_CLEAR_ENABLE(extra->db_depth_clear);
      ds_state->db_render_control |= S_028000_STENCIL_CLEAR_ENABLE(extra->db_stencil_clear);
      ds_state->db_render_control |= S_028000_RESUMMARIZE_ENABLE(extra->resummarize_enable);
      ds_state->db_render_control |= S_028000_DEPTH_COMPRESS_DISABLE(extra->depth_compress_disable);
      ds_state->db_render_control |= S_028000_STENCIL_COMPRESS_DISABLE(extra->stencil_compress_disable);
   }
}

void
radv_pipeline_init(struct radv_device *device, struct radv_pipeline *pipeline,
                    enum radv_pipeline_type type)
{
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);

   pipeline->device = device;
   pipeline->type = type;
}

static VkResult
radv_graphics_pipeline_init(struct radv_graphics_pipeline *pipeline, struct radv_device *device,
                            struct radv_pipeline_cache *cache,
                            const VkGraphicsPipelineCreateInfo *pCreateInfo,
                            const struct radv_graphics_pipeline_create_info *extra)
{
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   VkResult result;

   pipeline->last_vgt_api_stage = MESA_SHADER_NONE;

   /* Mark all states declared dynamic at pipeline creation. */
   if (pCreateInfo->pDynamicState) {
      uint32_t count = pCreateInfo->pDynamicState->dynamicStateCount;
      for (uint32_t s = 0; s < count; s++) {
         pipeline->dynamic_states |=
            radv_dynamic_state_mask(pCreateInfo->pDynamicState->pDynamicStates[s]);
      }
   }

   /* Mark all active stages at pipeline creation. */
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];

      pipeline->active_stages |= sinfo->stage;
   }

   struct radv_graphics_pipeline_info info = radv_pipeline_init_graphics_info(pipeline, pCreateInfo);

   struct radv_blend_state blend = radv_pipeline_init_blend_state(pipeline, pCreateInfo, &info);

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key =
      radv_generate_graphics_pipeline_key(pipeline, pCreateInfo, &info, &blend);

   result = radv_create_shaders(&pipeline->base, pipeline_layout, device, cache, &key, pCreateInfo->pStages,
                                pCreateInfo->stageCount, pCreateInfo->flags, NULL,
                                creation_feedback, NULL, NULL, &pipeline->last_vgt_api_stage);
   if (result != VK_SUCCESS)
      return result;

   pipeline->spi_baryc_cntl = S_0286E0_FRONT_FACE_ALL_BITS(1);

   uint32_t vgt_gs_out_prim_type = radv_pipeline_init_vgt_gs_out(pipeline, &info);

   radv_pipeline_init_multisample_state(pipeline, &blend, &info, vgt_gs_out_prim_type);

   if (!radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      radv_pipeline_init_input_assembly_state(pipeline, &info);
   radv_pipeline_init_dynamic_state(pipeline, &info);

   pipeline->negative_one_to_one = info.vp.negative_one_to_one;

   radv_pipeline_init_raster_state(pipeline, &info);

   struct radv_depth_stencil_state ds_state =
      radv_pipeline_init_depth_stencil_state(pipeline, &info);

   if (device->physical_device->rad_info.gfx_level >= GFX10_3)
      gfx103_pipeline_init_vrs_state(pipeline, &info);

   /* Ensure that some export memory is always allocated, for two reasons:
    *
    * 1) Correctness: The hardware ignores the EXEC mask if no export
    *    memory is allocated, so KILL and alpha test do not work correctly
    *    without this.
    * 2) Performance: Every shader needs at least a NULL export, even when
    *    it writes no color/depth output. The NULL export instruction
    *    stalls without this setting.
    *
    * Don't add this to CB_SHADER_MASK.
    *
    * GFX10 supports pixel shaders without exports by setting both the
    * color and Z formats to SPI_SHADER_ZERO. The hw will skip export
    * instructions if any are present.
    */
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   if ((device->physical_device->rad_info.gfx_level <= GFX9 || ps->info.ps.can_discard) &&
       !blend.spi_shader_col_format) {
      if (!ps->info.ps.writes_z && !ps->info.ps.writes_stencil && !ps->info.ps.writes_sample_mask)
         blend.spi_shader_col_format = V_028714_SPI_SHADER_32_R;
   }

   pipeline->col_format = blend.spi_shader_col_format;
   pipeline->cb_target_mask = blend.cb_target_mask;

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY) && !radv_pipeline_has_ngg(pipeline)) {
      struct radv_shader *gs = pipeline->base.shaders[MESA_SHADER_GEOMETRY];

      radv_pipeline_init_gs_ring_state(pipeline, &gs->info.gs_ring_info);
   }

   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL)) {
      pipeline->tess_patch_control_points = info.ts.patch_control_points;
   }

   if (!radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      radv_pipeline_init_vertex_input_state(pipeline, &info);

   radv_pipeline_init_binning_state(pipeline, &blend, &info);
   radv_pipeline_init_shader_stages_state(pipeline);
   radv_pipeline_init_scratch(device, &pipeline->base);

   /* Find the last vertex shader stage that eventually uses streamout. */
   pipeline->streamout_shader = radv_pipeline_get_streamout_shader(pipeline);

   pipeline->is_ngg = radv_pipeline_has_ngg(pipeline);
   pipeline->has_ngg_culling =
      pipeline->is_ngg &&
      pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.has_ngg_culling;
   pipeline->force_vrs_per_vertex =
      pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.force_vrs_per_vertex;
   pipeline->uses_user_sample_locations = info.ms.sample_locs_enable;
   pipeline->rast_prim = vgt_gs_out_prim_type;

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_LINE_WIDTH)) {
      pipeline->line_width = info.rs.line_width;
   }

   pipeline->base.push_constant_size = pipeline_layout->push_constant_size;
   pipeline->base.dynamic_offset_count = pipeline_layout->dynamic_offset_count;

   if (extra) {
      radv_pipeline_init_extra(pipeline, extra, &blend, &ds_state, &info, &vgt_gs_out_prim_type);
   }

   radv_pipeline_emit_pm4(pipeline, &blend, &ds_state, vgt_gs_out_prim_type, &info);

   return result;
}

static VkResult
radv_graphics_pipeline_create_nonlegacy(VkDevice _device, VkPipelineCache _cache,
                                        const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                        const struct radv_graphics_pipeline_create_info *extra,
                                        const VkAllocationCallbacks *pAllocator,
                                        VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
   struct radv_graphics_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_GRAPHICS);

   result = radv_graphics_pipeline_init(pipeline, device, cache, pCreateInfo, extra);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
      return result;
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

   return VK_SUCCESS;
}

/* This is a wrapper for radv_graphics_pipeline_create_nonlegacy that does all legacy conversions
 * for the VkGraphicsPipelineCreateInfo data. */
VkResult
radv_graphics_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                              const VkGraphicsPipelineCreateInfo *pCreateInfo,
                              const struct radv_graphics_pipeline_create_info *extra,
                              const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   VkGraphicsPipelineCreateInfo create_info = *pCreateInfo;

   VkPipelineRenderingCreateInfo rendering_create_info;
   VkFormat color_formats[MAX_RTS];
   VkAttachmentSampleCountInfoAMD sample_info;
   VkSampleCountFlagBits samples[MAX_RTS];
   if (pCreateInfo->renderPass != VK_NULL_HANDLE) {
      RADV_FROM_HANDLE(radv_render_pass, pass, pCreateInfo->renderPass);
      struct radv_subpass *subpass = pass->subpasses + pCreateInfo->subpass;

      rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
      rendering_create_info.pNext = create_info.pNext;
      create_info.pNext = &rendering_create_info;

      rendering_create_info.viewMask = subpass->view_mask;

      VkFormat ds_format =
         subpass->depth_stencil_attachment
            ? pass->attachments[subpass->depth_stencil_attachment->attachment].format
            : VK_FORMAT_UNDEFINED;

      rendering_create_info.depthAttachmentFormat =
         vk_format_has_depth(ds_format) ? ds_format : VK_FORMAT_UNDEFINED;
      rendering_create_info.stencilAttachmentFormat =
         vk_format_has_stencil(ds_format) ? ds_format : VK_FORMAT_UNDEFINED;

      rendering_create_info.colorAttachmentCount = subpass->color_count;
      rendering_create_info.pColorAttachmentFormats = color_formats;
      for (unsigned i = 0; i < rendering_create_info.colorAttachmentCount; ++i) {
         if (subpass->color_attachments[i].attachment != VK_ATTACHMENT_UNUSED)
            color_formats[i] = pass->attachments[subpass->color_attachments[i].attachment].format;
         else
            color_formats[i] = VK_FORMAT_UNDEFINED;
      }

      create_info.renderPass = VK_NULL_HANDLE;

      sample_info.sType = VK_STRUCTURE_TYPE_ATTACHMENT_SAMPLE_COUNT_INFO_AMD;
      sample_info.pNext = create_info.pNext;
      create_info.pNext = &sample_info;

      sample_info.colorAttachmentCount = rendering_create_info.colorAttachmentCount;
      sample_info.pColorAttachmentSamples = samples;
      for (unsigned i = 0; i < sample_info.colorAttachmentCount; ++i) {
         if (subpass->color_attachments[i].attachment != VK_ATTACHMENT_UNUSED) {
            samples[i] = pass->attachments[subpass->color_attachments[i].attachment].samples;
         } else
            samples[i] = 1;
      }
      sample_info.depthStencilAttachmentSamples = subpass->depth_sample_count;
   }

   return radv_graphics_pipeline_create_nonlegacy(_device, _cache, &create_info, extra, pAllocator,
                                                  pPipeline);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateGraphicsPipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                             const VkGraphicsPipelineCreateInfo *pCreateInfos,
                             const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r;
      r = radv_graphics_pipeline_create(_device, pipelineCache, &pCreateInfos[i], NULL, pAllocator,
                                        &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

void
radv_pipeline_emit_hw_cs(const struct radv_physical_device *pdevice, struct radeon_cmdbuf *cs,
                         const struct radv_shader *shader)
{
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg(cs, R_00B830_COMPUTE_PGM_LO, va >> 8);

   radeon_set_sh_reg_seq(cs, R_00B848_COMPUTE_PGM_RSRC1, 2);
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);
   if (pdevice->rad_info.gfx_level >= GFX10) {
      radeon_set_sh_reg(cs, R_00B8A0_COMPUTE_PGM_RSRC3, shader->config.rsrc3);
   }
}

void
radv_pipeline_emit_compute_state(const struct radv_physical_device *pdevice,
                                 struct radeon_cmdbuf *cs, const struct radv_shader *shader)
{
   unsigned threads_per_threadgroup;
   unsigned threadgroups_per_cu = 1;
   unsigned waves_per_threadgroup;
   unsigned max_waves_per_sh = 0;

   /* Calculate best compute resource limits. */
   threads_per_threadgroup =
      shader->info.cs.block_size[0] * shader->info.cs.block_size[1] * shader->info.cs.block_size[2];
   waves_per_threadgroup = DIV_ROUND_UP(threads_per_threadgroup, shader->info.wave_size);

   if (pdevice->rad_info.gfx_level >= GFX10 && waves_per_threadgroup == 1)
      threadgroups_per_cu = 2;

   radeon_set_sh_reg(
      cs, R_00B854_COMPUTE_RESOURCE_LIMITS,
      ac_get_compute_resource_limits(&pdevice->rad_info, waves_per_threadgroup,
                                     max_waves_per_sh, threadgroups_per_cu));

   radeon_set_sh_reg_seq(cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[0]));
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[1]));
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[2]));
}

static void
radv_compute_generate_pm4(struct radv_compute_pipeline *pipeline)
{
   struct radv_physical_device *pdevice = pipeline->base.device->physical_device;
   struct radv_shader *shader = pipeline->base.shaders[MESA_SHADER_COMPUTE];
   struct radeon_cmdbuf *cs = &pipeline->base.cs;

   cs->max_dw = pdevice->rad_info.gfx_level >= GFX10 ? 19 : 16;
   cs->buf = malloc(cs->max_dw * 4);

   radv_pipeline_emit_hw_cs(pdevice, cs, shader);
   radv_pipeline_emit_compute_state(pdevice, cs, shader);

   assert(pipeline->base.cs.cdw <= pipeline->base.cs.max_dw);
}

static struct radv_pipeline_key
radv_generate_compute_pipeline_key(struct radv_compute_pipeline *pipeline,
                                   const VkComputePipelineCreateInfo *pCreateInfo)
{
   const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->stage;
   struct radv_pipeline_key key = radv_generate_pipeline_key(&pipeline->base, pCreateInfo->flags);

   const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *subgroup_size =
      vk_find_struct_const(stage->pNext,
                           PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);

   if (subgroup_size) {
      assert(subgroup_size->requiredSubgroupSize == 32 ||
             subgroup_size->requiredSubgroupSize == 64);
      key.cs.compute_subgroup_size = subgroup_size->requiredSubgroupSize;
   } else if (stage->flags & VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT) {
      key.cs.require_full_subgroups = true;
   }

   return key;
}

VkResult
radv_compute_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                             const VkComputePipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, const uint8_t *custom_hash,
                             struct radv_pipeline_shader_stack_size *rt_stack_sizes,
                             uint32_t rt_group_count, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   struct radv_compute_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL) {
      free(rt_stack_sizes);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_COMPUTE);

   pipeline->rt_stack_sizes = rt_stack_sizes;
   pipeline->group_count = rt_group_count;

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key = radv_generate_compute_pipeline_key(pipeline, pCreateInfo);

   UNUSED gl_shader_stage last_vgt_api_stage = MESA_SHADER_NONE;
   result = radv_create_shaders(&pipeline->base, pipeline_layout, device, cache, &key, &pCreateInfo->stage,
                                1, pCreateInfo->flags, custom_hash, creation_feedback,
                                &pipeline->rt_stack_sizes, &pipeline->group_count,
                                &last_vgt_api_stage);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
      return result;
   }

   pipeline->base.user_data_0[MESA_SHADER_COMPUTE] = R_00B900_COMPUTE_USER_DATA_0;
   pipeline->base.need_indirect_descriptor_sets |=
      radv_shader_need_indirect_descriptor_sets(&pipeline->base, MESA_SHADER_COMPUTE);
   radv_pipeline_init_scratch(device, &pipeline->base);

   pipeline->base.push_constant_size = pipeline_layout->push_constant_size;
   pipeline->base.dynamic_offset_count = pipeline_layout->dynamic_offset_count;

   if (device->physical_device->rad_info.has_cs_regalloc_hang_bug) {
      struct radv_shader *compute_shader = pipeline->base.shaders[MESA_SHADER_COMPUTE];
      unsigned *cs_block_size = compute_shader->info.cs.block_size;

      pipeline->cs_regalloc_hang_bug = cs_block_size[0] * cs_block_size[1] * cs_block_size[2] > 256;
   }

   radv_compute_generate_pm4(pipeline);

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateComputePipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                            const VkComputePipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_compute_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator, NULL,
                                       NULL, 0, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

static uint32_t
radv_get_executable_count(struct radv_pipeline *pipeline)
{
   uint32_t ret = 0;
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!pipeline->shaders[i])
         continue;

      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         ret += 2u;
      } else {
         ret += 1u;
      }
   }
   return ret;
}

static struct radv_shader *
radv_get_shader_from_executable_index(struct radv_pipeline *pipeline, int index,
                                      gl_shader_stage *stage)
{
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (!pipeline->shaders[i])
         continue;
      if (!index) {
         *stage = i;
         return pipeline->shaders[i];
      }

      --index;

      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         if (!index) {
            *stage = i;
            return pipeline->gs_copy_shader;
         }
         --index;
      }
   }

   *stage = -1;
   return NULL;
}

/* Basically strlcpy (which does not exist on linux) specialized for
 * descriptions. */
static void
desc_copy(char *desc, const char *src)
{
   int len = strlen(src);
   assert(len < VK_MAX_DESCRIPTION_SIZE);
   memcpy(desc, src, len);
   memset(desc + len, 0, VK_MAX_DESCRIPTION_SIZE - len);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutablePropertiesKHR(VkDevice _device, const VkPipelineInfoKHR *pPipelineInfo,
                                        uint32_t *pExecutableCount,
                                        VkPipelineExecutablePropertiesKHR *pProperties)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pPipelineInfo->pipeline);
   const uint32_t total_count = radv_get_executable_count(pipeline);

   if (!pProperties) {
      *pExecutableCount = total_count;
      return VK_SUCCESS;
   }

   const uint32_t count = MIN2(total_count, *pExecutableCount);
   for (unsigned i = 0, executable_idx = 0; i < MESA_VULKAN_SHADER_STAGES && executable_idx < count; ++i) {
      if (!pipeline->shaders[i])
         continue;
      pProperties[executable_idx].stages = mesa_to_vk_shader_stage(i);
      const char *name = NULL;
      const char *description = NULL;
      switch (i) {
      case MESA_SHADER_VERTEX:
         name = "Vertex Shader";
         description = "Vulkan Vertex Shader";
         break;
      case MESA_SHADER_TESS_CTRL:
         if (!pipeline->shaders[MESA_SHADER_VERTEX]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_VERTEX_BIT;
            name = "Vertex + Tessellation Control Shaders";
            description = "Combined Vulkan Vertex and Tessellation Control Shaders";
         } else {
            name = "Tessellation Control Shader";
            description = "Vulkan Tessellation Control Shader";
         }
         break;
      case MESA_SHADER_TESS_EVAL:
         name = "Tessellation Evaluation Shader";
         description = "Vulkan Tessellation Evaluation Shader";
         break;
      case MESA_SHADER_GEOMETRY:
         if (pipeline->shaders[MESA_SHADER_TESS_CTRL] && !pipeline->shaders[MESA_SHADER_TESS_EVAL]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            name = "Tessellation Evaluation + Geometry Shaders";
            description = "Combined Vulkan Tessellation Evaluation and Geometry Shaders";
         } else if (!pipeline->shaders[MESA_SHADER_TESS_CTRL] && !pipeline->shaders[MESA_SHADER_VERTEX]) {
            pProperties[executable_idx].stages |= VK_SHADER_STAGE_VERTEX_BIT;
            name = "Vertex + Geometry Shader";
            description = "Combined Vulkan Vertex and Geometry Shaders";
         } else {
            name = "Geometry Shader";
            description = "Vulkan Geometry Shader";
         }
         break;
      case MESA_SHADER_FRAGMENT:
         name = "Fragment Shader";
         description = "Vulkan Fragment Shader";
         break;
      case MESA_SHADER_COMPUTE:
         name = "Compute Shader";
         description = "Vulkan Compute Shader";
         break;
      case MESA_SHADER_MESH:
         name = "Mesh Shader";
         description = "Vulkan Mesh Shader";
         break;
      case MESA_SHADER_TASK:
         name = "Task Shader";
         description = "Vulkan Task Shader";
         break;
      }

      pProperties[executable_idx].subgroupSize = pipeline->shaders[i]->info.wave_size;
      desc_copy(pProperties[executable_idx].name, name);
      desc_copy(pProperties[executable_idx].description, description);

      ++executable_idx;
      if (i == MESA_SHADER_GEOMETRY &&
          !radv_pipeline_has_ngg(radv_pipeline_to_graphics(pipeline))) {
         assert(pipeline->gs_copy_shader);
         if (executable_idx >= count)
            break;

         pProperties[executable_idx].stages = VK_SHADER_STAGE_GEOMETRY_BIT;
         pProperties[executable_idx].subgroupSize = 64;
         desc_copy(pProperties[executable_idx].name, "GS Copy Shader");
         desc_copy(pProperties[executable_idx].description,
                   "Extra shader stage that loads the GS output ringbuffer into the rasterizer");

         ++executable_idx;
      }
   }

   VkResult result = *pExecutableCount < total_count ? VK_INCOMPLETE : VK_SUCCESS;
   *pExecutableCount = count;
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutableStatisticsKHR(VkDevice _device,
                                        const VkPipelineExecutableInfoKHR *pExecutableInfo,
                                        uint32_t *pStatisticCount,
                                        VkPipelineExecutableStatisticKHR *pStatistics)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pExecutableInfo->pipeline);
   gl_shader_stage stage;
   struct radv_shader *shader =
      radv_get_shader_from_executable_index(pipeline, pExecutableInfo->executableIndex, &stage);

   const struct radv_physical_device *pdevice = device->physical_device;

   unsigned lds_increment = pdevice->rad_info.gfx_level >= GFX11 && stage == MESA_SHADER_FRAGMENT
      ? 1024 : pdevice->rad_info.lds_encode_granularity;
   unsigned max_waves = radv_get_max_waves(device, shader, stage);

   VkPipelineExecutableStatisticKHR *s = pStatistics;
   VkPipelineExecutableStatisticKHR *end = s + (pStatistics ? *pStatisticCount : 0);
   VkResult result = VK_SUCCESS;

   if (s < end) {
      desc_copy(s->name, "Driver pipeline hash");
      desc_copy(s->description, "Driver pipeline hash used by RGP");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = pipeline->pipeline_hash;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "SGPRs");
      desc_copy(s->description, "Number of SGPR registers allocated per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.num_sgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "VGPRs");
      desc_copy(s->description, "Number of VGPR registers allocated per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.num_vgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Spilled SGPRs");
      desc_copy(s->description, "Number of SGPR registers spilled per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.spilled_sgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Spilled VGPRs");
      desc_copy(s->description, "Number of VGPR registers spilled per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.spilled_vgprs;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Code size");
      desc_copy(s->description, "Code size in bytes");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->exec_size;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "LDS size");
      desc_copy(s->description, "LDS size in bytes per workgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.lds_size * lds_increment;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Scratch size");
      desc_copy(s->description, "Private memory in bytes per subgroup");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = shader->config.scratch_bytes_per_wave;
   }
   ++s;

   if (s < end) {
      desc_copy(s->name, "Subgroups per SIMD");
      desc_copy(s->description, "The maximum number of subgroups in flight on a SIMD unit");
      s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      s->value.u64 = max_waves;
   }
   ++s;

   if (shader->statistics) {
      for (unsigned i = 0; i < aco_num_statistics; i++) {
         const struct aco_compiler_statistic_info *info = &aco_statistic_infos[i];
         if (s < end) {
            desc_copy(s->name, info->name);
            desc_copy(s->description, info->desc);
            s->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
            s->value.u64 = shader->statistics[i];
         }
         ++s;
      }
   }

   if (!pStatistics)
      *pStatisticCount = s - pStatistics;
   else if (s > end) {
      *pStatisticCount = end - pStatistics;
      result = VK_INCOMPLETE;
   } else {
      *pStatisticCount = s - pStatistics;
   }

   return result;
}

static VkResult
radv_copy_representation(void *data, size_t *data_size, const char *src)
{
   size_t total_size = strlen(src) + 1;

   if (!data) {
      *data_size = total_size;
      return VK_SUCCESS;
   }

   size_t size = MIN2(total_size, *data_size);

   memcpy(data, src, size);
   if (size)
      *((char *)data + size - 1) = 0;
   return size < total_size ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPipelineExecutableInternalRepresentationsKHR(
   VkDevice _device, const VkPipelineExecutableInfoKHR *pExecutableInfo,
   uint32_t *pInternalRepresentationCount,
   VkPipelineExecutableInternalRepresentationKHR *pInternalRepresentations)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline, pipeline, pExecutableInfo->pipeline);
   gl_shader_stage stage;
   struct radv_shader *shader =
      radv_get_shader_from_executable_index(pipeline, pExecutableInfo->executableIndex, &stage);

   VkPipelineExecutableInternalRepresentationKHR *p = pInternalRepresentations;
   VkPipelineExecutableInternalRepresentationKHR *end =
      p + (pInternalRepresentations ? *pInternalRepresentationCount : 0);
   VkResult result = VK_SUCCESS;
   /* optimized NIR */
   if (p < end) {
      p->isText = true;
      desc_copy(p->name, "NIR Shader(s)");
      desc_copy(p->description, "The optimized NIR shader(s)");
      if (radv_copy_representation(p->pData, &p->dataSize, shader->nir_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   /* backend IR */
   if (p < end) {
      p->isText = true;
      if (radv_use_llvm_for_stage(device, stage)) {
         desc_copy(p->name, "LLVM IR");
         desc_copy(p->description, "The LLVM IR after some optimizations");
      } else {
         desc_copy(p->name, "ACO IR");
         desc_copy(p->description, "The ACO IR after some optimizations");
      }
      if (radv_copy_representation(p->pData, &p->dataSize, shader->ir_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   /* Disassembler */
   if (p < end && shader->disasm_string) {
      p->isText = true;
      desc_copy(p->name, "Assembly");
      desc_copy(p->description, "Final Assembly");
      if (radv_copy_representation(p->pData, &p->dataSize, shader->disasm_string) != VK_SUCCESS)
         result = VK_INCOMPLETE;
   }
   ++p;

   if (!pInternalRepresentations)
      *pInternalRepresentationCount = p - pInternalRepresentations;
   else if (p > end) {
      result = VK_INCOMPLETE;
      *pInternalRepresentationCount = end - pInternalRepresentations;
   } else {
      *pInternalRepresentationCount = p - pInternalRepresentations;
   }

   return result;
}
