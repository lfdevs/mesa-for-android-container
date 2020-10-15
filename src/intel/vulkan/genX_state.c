/*
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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

#include "common/gen_aux_map.h"
#include "common/gen_sample_positions.h"
#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

#include "vk_util.h"

static void
genX(emit_slice_hashing_state)(struct anv_device *device,
                               struct anv_batch *batch)
{
   device->slice_hash = (struct anv_state) { 0 };

#if GEN_GEN == 11
   const unsigned *ppipe_subslices = device->info.ppipe_subslices;
   int subslices_delta = ppipe_subslices[0] - ppipe_subslices[1];
   if (subslices_delta == 0)
      return;

   unsigned size = GENX(SLICE_HASH_TABLE_length) * 4;
   device->slice_hash =
      anv_state_pool_alloc(&device->dynamic_state_pool, size, 64);

   struct GENX(SLICE_HASH_TABLE) table0 = {
      .Entry = {
         { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 },
         { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
         { 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0 },
         { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 },
         { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
         { 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0 },
         { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 },
         { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
         { 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0 },
         { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 },
         { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
         { 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0 },
         { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 },
         { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1 },
         { 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0 },
         { 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 }
      }
   };

   struct GENX(SLICE_HASH_TABLE) table1 = {
      .Entry = {
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 },
         { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 },
         { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 },
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 },
         { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 },
         { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 },
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 },
         { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 },
         { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 },
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 },
         { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 },
         { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 },
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 },
         { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 },
         { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 },
         { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 }
      }
   };

   const struct GENX(SLICE_HASH_TABLE) *table =
      subslices_delta < 0 ? &table0 : &table1;
   GENX(SLICE_HASH_TABLE_pack)(NULL, device->slice_hash.map, table);

   anv_batch_emit(batch, GENX(3DSTATE_SLICE_TABLE_STATE_POINTERS), ptr) {
      ptr.SliceHashStatePointerValid = true;
      ptr.SliceHashTableStatePointer = device->slice_hash.offset;
   }

   anv_batch_emit(batch, GENX(3DSTATE_3D_MODE), mode) {
      mode.SliceHashingTableEnable = true;
   }
#endif
}

VkResult
genX(init_device_state)(struct anv_device *device)
{
   struct anv_batch batch;

   uint32_t cmds[64];
   batch.start = batch.next = cmds;
   batch.end = (void *) cmds + sizeof(cmds);

   anv_batch_emit(&batch, GENX(PIPELINE_SELECT), ps) {
#if GEN_GEN >= 9
      ps.MaskBits = GEN_GEN >= 12 ? 0x13 : 3;
      ps.MediaSamplerDOPClockGateEnable = GEN_GEN >= 12;
#endif
      ps.PipelineSelection = _3D;
   }

#if GEN_GEN == 9
   uint32_t cache_mode_1;
   anv_pack_struct(&cache_mode_1, GENX(CACHE_MODE_1),
                   .FloatBlendOptimizationEnable = true,
                   .FloatBlendOptimizationEnableMask = true,
                   .MSCRAWHazardAvoidanceBit = true,
                   .MSCRAWHazardAvoidanceBitMask = true,
                   .PartialResolveDisableInVC = true,
                   .PartialResolveDisableInVCMask = true);

   anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset = GENX(CACHE_MODE_1_num);
      lri.DataDWord      = cache_mode_1;
   }
#endif

   anv_batch_emit(&batch, GENX(3DSTATE_AA_LINE_PARAMETERS), aa);

   anv_batch_emit(&batch, GENX(3DSTATE_DRAWING_RECTANGLE), rect) {
      rect.ClippedDrawingRectangleYMin = 0;
      rect.ClippedDrawingRectangleXMin = 0;
      rect.ClippedDrawingRectangleYMax = UINT16_MAX;
      rect.ClippedDrawingRectangleXMax = UINT16_MAX;
      rect.DrawingRectangleOriginY = 0;
      rect.DrawingRectangleOriginX = 0;
   }

#if GEN_GEN >= 8
   anv_batch_emit(&batch, GENX(3DSTATE_WM_CHROMAKEY), ck);

   /* See the Vulkan 1.0 spec Table 24.1 "Standard sample locations" and
    * VkPhysicalDeviceFeatures::standardSampleLocations.
    */
   anv_batch_emit(&batch, GENX(3DSTATE_SAMPLE_PATTERN), sp) {
      GEN_SAMPLE_POS_1X(sp._1xSample);
      GEN_SAMPLE_POS_2X(sp._2xSample);
      GEN_SAMPLE_POS_4X(sp._4xSample);
      GEN_SAMPLE_POS_8X(sp._8xSample);
#if GEN_GEN >= 9
      GEN_SAMPLE_POS_16X(sp._16xSample);
#endif
   }

   /* The BDW+ docs describe how to use the 3DSTATE_WM_HZ_OP instruction in the
    * section titled, "Optimized Depth Buffer Clear and/or Stencil Buffer
    * Clear." It mentions that the packet overrides GPU state for the clear
    * operation and needs to be reset to 0s to clear the overrides. Depending
    * on the kernel, we may not get a context with the state for this packet
    * zeroed. Do it ourselves just in case. We've observed this to prevent a
    * number of GPU hangs on ICL.
    */
   anv_batch_emit(&batch, GENX(3DSTATE_WM_HZ_OP), hzp);
#endif

#if GEN_GEN == 11
   /* The default behavior of bit 5 "Headerless Message for Pre-emptable
    * Contexts" in SAMPLER MODE register is set to 0, which means
    * headerless sampler messages are not allowed for pre-emptable
    * contexts. Set the bit 5 to 1 to allow them.
    */
   uint32_t sampler_mode;
   anv_pack_struct(&sampler_mode, GENX(SAMPLER_MODE),
                   .HeaderlessMessageforPreemptableContexts = true,
                   .HeaderlessMessageforPreemptableContextsMask = true);

    anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset = GENX(SAMPLER_MODE_num);
      lri.DataDWord      = sampler_mode;
   }

   /* Bit 1 "Enabled Texel Offset Precision Fix" must be set in
    * HALF_SLICE_CHICKEN7 register.
    */
   uint32_t half_slice_chicken7;
   anv_pack_struct(&half_slice_chicken7, GENX(HALF_SLICE_CHICKEN7),
                   .EnabledTexelOffsetPrecisionFix = true,
                   .EnabledTexelOffsetPrecisionFixMask = true);

    anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset = GENX(HALF_SLICE_CHICKEN7_num);
      lri.DataDWord      = half_slice_chicken7;
   }

   uint32_t tccntlreg;
   anv_pack_struct(&tccntlreg, GENX(TCCNTLREG),
                   .L3DataPartialWriteMergingEnable = true,
                   .ColorZPartialWriteMergingEnable = true,
                   .URBPartialWriteMergingEnable = true,
                   .TCDisable = true);

   anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset = GENX(TCCNTLREG_num);
      lri.DataDWord      = tccntlreg;
   }

#endif
   genX(emit_slice_hashing_state)(device, &batch);

#if GEN_GEN >= 11
   /* hardware specification recommends disabling repacking for
    * the compatibility with decompression mechanism in display controller.
    */
   if (device->info.disable_ccs_repack) {
      uint32_t cache_mode_0;
      anv_pack_struct(&cache_mode_0,
                      GENX(CACHE_MODE_0),
                      .DisableRepackingforCompression = true,
                      .DisableRepackingforCompressionMask = true);

      anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
         lri.RegisterOffset = GENX(CACHE_MODE_0_num);
         lri.DataDWord      = cache_mode_0;
      }
   }

   /* an unknown issue is causing vs push constants to become
    * corrupted during object-level preemption. For now, restrict
    * to command buffer level preemption to avoid rendering
    * corruption.
    */
   uint32_t cs_chicken1;
   anv_pack_struct(&cs_chicken1,
                   GENX(CS_CHICKEN1),
                   .ReplayMode = MidcmdbufferPreemption,
                   .ReplayModeMask = true);

   anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset = GENX(CS_CHICKEN1_num);
      lri.DataDWord      = cs_chicken1;
   }
#endif

#if GEN_GEN == 12
   if (device->info.has_aux_map) {
      uint64_t aux_base_addr = gen_aux_map_get_base(device->aux_map_ctx);
      assert(aux_base_addr % (32 * 1024) == 0);
      anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
         lri.RegisterOffset = GENX(GFX_AUX_TABLE_BASE_ADDR_num);
         lri.DataDWord = aux_base_addr & 0xffffffff;
      }
      anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
         lri.RegisterOffset = GENX(GFX_AUX_TABLE_BASE_ADDR_num) + 4;
         lri.DataDWord = aux_base_addr >> 32;
      }
   }
#endif

   /* Set the "CONSTANT_BUFFER Address Offset Disable" bit, so
    * 3DSTATE_CONSTANT_XS buffer 0 is an absolute address.
    *
    * This is only safe on kernels with context isolation support.
    */
   if (GEN_GEN >= 8 && device->physical->has_context_isolation) {
      UNUSED uint32_t tmp_reg;
#if GEN_GEN >= 9
      anv_pack_struct(&tmp_reg, GENX(CS_DEBUG_MODE2),
                      .CONSTANT_BUFFERAddressOffsetDisable = true,
                      .CONSTANT_BUFFERAddressOffsetDisableMask = true);
      anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
         lri.RegisterOffset = GENX(CS_DEBUG_MODE2_num);
         lri.DataDWord      = tmp_reg;
      }
#elif GEN_GEN == 8
      anv_pack_struct(&tmp_reg, GENX(INSTPM),
                      .CONSTANT_BUFFERAddressOffsetDisable = true,
                      .CONSTANT_BUFFERAddressOffsetDisableMask = true);
      anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
         lri.RegisterOffset = GENX(INSTPM_num);
         lri.DataDWord      = tmp_reg;
      }
#endif
   }

#if GEN_GEN >= 12
   const struct gen_l3_config *cfg = gen_get_default_l3_config(&device->info);
   if (!cfg) {
      /* Platforms with no configs just setup full-way allocation. */
      uint32_t l3cr;
      anv_pack_struct(&l3cr, GENX(L3ALLOC),
                      .L3FullWayAllocationEnable = true);
      anv_batch_emit(&batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
         lri.RegisterOffset = GENX(L3ALLOC_num);
         lri.DataDWord      = l3cr;
      }
   }
#endif

   anv_batch_emit(&batch, GENX(MI_BATCH_BUFFER_END), bbe);

   assert(batch.next <= batch.end);

   return anv_queue_submit_simple_batch(&device->queue, &batch);
}

static uint32_t
vk_to_gen_tex_filter(VkFilter filter, bool anisotropyEnable)
{
   switch (filter) {
   default:
      assert(!"Invalid filter");
   case VK_FILTER_NEAREST:
      return anisotropyEnable ? MAPFILTER_ANISOTROPIC : MAPFILTER_NEAREST;
   case VK_FILTER_LINEAR:
      return anisotropyEnable ? MAPFILTER_ANISOTROPIC : MAPFILTER_LINEAR;
   }
}

static uint32_t
vk_to_gen_max_anisotropy(float ratio)
{
   return (anv_clamp_f(ratio, 2, 16) - 2) / 2;
}

static const uint32_t vk_to_gen_mipmap_mode[] = {
   [VK_SAMPLER_MIPMAP_MODE_NEAREST]          = MIPFILTER_NEAREST,
   [VK_SAMPLER_MIPMAP_MODE_LINEAR]           = MIPFILTER_LINEAR
};

static const uint32_t vk_to_gen_tex_address[] = {
   [VK_SAMPLER_ADDRESS_MODE_REPEAT]          = TCM_WRAP,
   [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT] = TCM_MIRROR,
   [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE]   = TCM_CLAMP,
   [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE] = TCM_MIRROR_ONCE,
   [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER] = TCM_CLAMP_BORDER,
};

/* Vulkan specifies the result of shadow comparisons as:
 *     1     if   ref <op> texel,
 *     0     otherwise.
 *
 * The hardware does:
 *     0     if texel <op> ref,
 *     1     otherwise.
 *
 * So, these look a bit strange because there's both a negation
 * and swapping of the arguments involved.
 */
static const uint32_t vk_to_gen_shadow_compare_op[] = {
   [VK_COMPARE_OP_NEVER]                        = PREFILTEROPALWAYS,
   [VK_COMPARE_OP_LESS]                         = PREFILTEROPLEQUAL,
   [VK_COMPARE_OP_EQUAL]                        = PREFILTEROPNOTEQUAL,
   [VK_COMPARE_OP_LESS_OR_EQUAL]                = PREFILTEROPLESS,
   [VK_COMPARE_OP_GREATER]                      = PREFILTEROPGEQUAL,
   [VK_COMPARE_OP_NOT_EQUAL]                    = PREFILTEROPEQUAL,
   [VK_COMPARE_OP_GREATER_OR_EQUAL]             = PREFILTEROPGREATER,
   [VK_COMPARE_OP_ALWAYS]                       = PREFILTEROPNEVER,
};

#if GEN_GEN >= 9
static const uint32_t vk_to_gen_sampler_reduction_mode[] = {
   [VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT] = STD_FILTER,
   [VK_SAMPLER_REDUCTION_MODE_MIN_EXT]              = MINIMUM,
   [VK_SAMPLER_REDUCTION_MODE_MAX_EXT]              = MAXIMUM,
};
#endif

VkResult genX(CreateSampler)(
    VkDevice                                    _device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*sampler), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sampler->base, VK_OBJECT_TYPE_SAMPLER);
   sampler->n_planes = 1;

   uint32_t border_color_stride = GEN_IS_HASWELL ? 512 : 64;
   uint32_t border_color_offset;
   ASSERTED bool has_custom_color = false;
   if (pCreateInfo->borderColor <= VK_BORDER_COLOR_INT_OPAQUE_WHITE) {
      border_color_offset = device->border_colors.offset +
                            pCreateInfo->borderColor *
                            border_color_stride;
   } else {
      assert(GEN_GEN >= 8);
      sampler->custom_border_color =
         anv_state_reserved_pool_alloc(&device->custom_border_colors);
      border_color_offset = sampler->custom_border_color.offset;
   }

#if GEN_GEN >= 9
   unsigned sampler_reduction_mode = STD_FILTER;
   bool enable_sampler_reduction = false;
#endif

   vk_foreach_struct(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO: {
         VkSamplerYcbcrConversionInfo *pSamplerConversion =
            (VkSamplerYcbcrConversionInfo *) ext;
         ANV_FROM_HANDLE(anv_ycbcr_conversion, conversion,
                         pSamplerConversion->conversion);

         /* Ignore conversion for non-YUV formats. This fulfills a requirement
          * for clients that want to utilize same code path for images with
          * external formats (VK_FORMAT_UNDEFINED) and "regular" RGBA images
          * where format is known.
          */
         if (conversion == NULL || !conversion->format->can_ycbcr)
            break;

         sampler->n_planes = conversion->format->n_planes;
         sampler->conversion = conversion;
         break;
      }
#if GEN_GEN >= 9
      case VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO: {
         VkSamplerReductionModeCreateInfo *sampler_reduction =
            (VkSamplerReductionModeCreateInfo *) ext;
         sampler_reduction_mode =
            vk_to_gen_sampler_reduction_mode[sampler_reduction->reductionMode];
         enable_sampler_reduction = true;
         break;
      }
#endif
      case VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT: {
         VkSamplerCustomBorderColorCreateInfoEXT *custom_border_color =
            (VkSamplerCustomBorderColorCreateInfoEXT *) ext;
         if (sampler->custom_border_color.map == NULL)
            break;
         struct gen8_border_color *cbc = sampler->custom_border_color.map;
         if (custom_border_color->format == VK_FORMAT_B4G4R4A4_UNORM_PACK16) {
            /* B4G4R4A4_UNORM_PACK16 is treated as R4G4B4A4_UNORM_PACK16 with
             * a swizzle, but this does not carry over to the sampler for
             * border colors, so we need to do the swizzle ourselves here.
             */
            cbc->uint32[0] = custom_border_color->customBorderColor.uint32[2];
            cbc->uint32[1] = custom_border_color->customBorderColor.uint32[1];
            cbc->uint32[2] = custom_border_color->customBorderColor.uint32[0];
            cbc->uint32[3] = custom_border_color->customBorderColor.uint32[3];
         } else {
            /* Both structs share the same layout, so just copy them over. */
            memcpy(cbc, &custom_border_color->customBorderColor,
                   sizeof(VkClearColorValue));
         }
         has_custom_color = true;
         break;
      }
      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   assert((sampler->custom_border_color.map == NULL) || has_custom_color);

   if (device->physical->has_bindless_samplers) {
      /* If we have bindless, allocate enough samplers.  We allocate 32 bytes
       * for each sampler instead of 16 bytes because we want all bindless
       * samplers to be 32-byte aligned so we don't have to use indirect
       * sampler messages on them.
       */
      sampler->bindless_state =
         anv_state_pool_alloc(&device->dynamic_state_pool,
                              sampler->n_planes * 32, 32);
   }

   for (unsigned p = 0; p < sampler->n_planes; p++) {
      const bool plane_has_chroma =
         sampler->conversion && sampler->conversion->format->planes[p].has_chroma;
      const VkFilter min_filter =
         plane_has_chroma ? sampler->conversion->chroma_filter : pCreateInfo->minFilter;
      const VkFilter mag_filter =
         plane_has_chroma ? sampler->conversion->chroma_filter : pCreateInfo->magFilter;
      const bool enable_min_filter_addr_rounding = min_filter != VK_FILTER_NEAREST;
      const bool enable_mag_filter_addr_rounding = mag_filter != VK_FILTER_NEAREST;
      /* From Broadwell PRM, SAMPLER_STATE:
       *   "Mip Mode Filter must be set to MIPFILTER_NONE for Planar YUV surfaces."
       */
      const uint32_t mip_filter_mode =
         (sampler->conversion &&
          isl_format_is_yuv(sampler->conversion->format->planes[0].isl_format)) ?
         MIPFILTER_NONE : vk_to_gen_mipmap_mode[pCreateInfo->mipmapMode];

      struct GENX(SAMPLER_STATE) sampler_state = {
         .SamplerDisable = false,
         .TextureBorderColorMode = DX10OGL,

#if GEN_GEN >= 8
         .LODPreClampMode = CLAMP_MODE_OGL,
#else
         .LODPreClampEnable = CLAMP_ENABLE_OGL,
#endif

#if GEN_GEN == 8
         .BaseMipLevel = 0.0,
#endif
         .MipModeFilter = mip_filter_mode,
         .MagModeFilter = vk_to_gen_tex_filter(mag_filter, pCreateInfo->anisotropyEnable),
         .MinModeFilter = vk_to_gen_tex_filter(min_filter, pCreateInfo->anisotropyEnable),
         .TextureLODBias = anv_clamp_f(pCreateInfo->mipLodBias, -16, 15.996),
         .AnisotropicAlgorithm =
            pCreateInfo->anisotropyEnable ? EWAApproximation : LEGACY,
         .MinLOD = anv_clamp_f(pCreateInfo->minLod, 0, 14),
         .MaxLOD = anv_clamp_f(pCreateInfo->maxLod, 0, 14),
         .ChromaKeyEnable = 0,
         .ChromaKeyIndex = 0,
         .ChromaKeyMode = 0,
         .ShadowFunction =
            vk_to_gen_shadow_compare_op[pCreateInfo->compareEnable ?
                                        pCreateInfo->compareOp : VK_COMPARE_OP_NEVER],
         .CubeSurfaceControlMode = OVERRIDE,

         .BorderColorPointer = border_color_offset,

#if GEN_GEN >= 8
         .LODClampMagnificationMode = MIPNONE,
#endif

         .MaximumAnisotropy = vk_to_gen_max_anisotropy(pCreateInfo->maxAnisotropy),
         .RAddressMinFilterRoundingEnable = enable_min_filter_addr_rounding,
         .RAddressMagFilterRoundingEnable = enable_mag_filter_addr_rounding,
         .VAddressMinFilterRoundingEnable = enable_min_filter_addr_rounding,
         .VAddressMagFilterRoundingEnable = enable_mag_filter_addr_rounding,
         .UAddressMinFilterRoundingEnable = enable_min_filter_addr_rounding,
         .UAddressMagFilterRoundingEnable = enable_mag_filter_addr_rounding,
         .TrilinearFilterQuality = 0,
         .NonnormalizedCoordinateEnable = pCreateInfo->unnormalizedCoordinates,
         .TCXAddressControlMode = vk_to_gen_tex_address[pCreateInfo->addressModeU],
         .TCYAddressControlMode = vk_to_gen_tex_address[pCreateInfo->addressModeV],
         .TCZAddressControlMode = vk_to_gen_tex_address[pCreateInfo->addressModeW],

#if GEN_GEN >= 9
         .ReductionType = sampler_reduction_mode,
         .ReductionTypeEnable = enable_sampler_reduction,
#endif
      };

      GENX(SAMPLER_STATE_pack)(NULL, sampler->state[p], &sampler_state);

      if (sampler->bindless_state.map) {
         memcpy(sampler->bindless_state.map + p * 32,
                sampler->state[p], GENX(SAMPLER_STATE_length) * 4);
      }
   }

   *pSampler = anv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}
