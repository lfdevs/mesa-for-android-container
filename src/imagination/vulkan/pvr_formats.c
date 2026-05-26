/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "drm-uapi/drm_fourcc.h"
#include "hwdef/rogue_hw_utils.h"

#include "pvr_common.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_limits.h"
#include "pvr_formats.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"

#include "util/bitpack_helpers.h"
#include "util/bitscan.h"
#include "util/compiler.h"
#include "util/format/format_utils.h"
#include "util/format/u_formats.h"
#include "util/half_float.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vk_enum_defines.h"
#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

uint32_t pvr_get_pbe_accum_format_size_in_bytes(VkFormat vk_format)
{
   enum pvr_pbe_accum_format pbe_accum_format;
   uint32_t nr_components;

   /* TODO: Decouple from arch-specific function */
   pbe_accum_format = pvr_rogue_get_pbe_accum_format(vk_format);
   nr_components = vk_format_get_nr_components(vk_format);

   switch (pbe_accum_format) {
   case PVR_PBE_ACCUM_FORMAT_U8:
   case PVR_PBE_ACCUM_FORMAT_S8:
   case PVR_PBE_ACCUM_FORMAT_UINT8:
   case PVR_PBE_ACCUM_FORMAT_SINT8:
      return nr_components * 1;

   case PVR_PBE_ACCUM_FORMAT_U16:
   case PVR_PBE_ACCUM_FORMAT_S16:
   case PVR_PBE_ACCUM_FORMAT_F16:
   case PVR_PBE_ACCUM_FORMAT_UINT16:
   case PVR_PBE_ACCUM_FORMAT_SINT16:
      return nr_components * 2;

   case PVR_PBE_ACCUM_FORMAT_F32:
   case PVR_PBE_ACCUM_FORMAT_UINT32:
   case PVR_PBE_ACCUM_FORMAT_SINT32:
   case PVR_PBE_ACCUM_FORMAT_UINT32_MEDP:
   case PVR_PBE_ACCUM_FORMAT_SINT32_MEDP:
   case PVR_PBE_ACCUM_FORMAT_U24:
      return nr_components * 4;

   case PVR_PBE_ACCUM_FORMAT_U1010102:
      assert(nr_components == 4);
      return 4;

   default:
      UNREACHABLE("Unknown pbe accum format. Implementation error");
   }
}

/**
 * \brief Packs VK_FORMAT_A2B10G10R10_UINT_PACK32 or A2R10G10B10.
 *
 * \param[in] values   RGBA ordered values to pack.
 * \param[in] swap_rb  If true pack A2B10G10R10 else pack A2R10G10B10.
 */
static inline uint32_t pvr_pack_a2x10y10z10_uint(
   const uint32_t values[static const PVR_CLEAR_COLOR_ARRAY_SIZE],
   bool swap_rb)
{
   const uint32_t blue = swap_rb ? values[0] : values[2];
   const uint32_t red = swap_rb ? values[2] : values[0];
   uint32_t packed_val;

   /* The user is allowed to specify a value which is over the range
    * representable for a component so we need to AND before packing.
    */

   packed_val = util_bitpack_uint(values[3] & BITSET_MASK(2), 30, 31);
   packed_val |= util_bitpack_uint(red & BITSET_MASK(10), 20, 29);
   packed_val |= util_bitpack_uint(values[1] & BITSET_MASK(10), 10, 19);
   packed_val |= util_bitpack_uint(blue & BITSET_MASK(10), 0, 9);

   return packed_val;
}

#define APPLY_FUNC_4V(DST, FUNC, ARG) \
   ASSIGN_4V(DST, FUNC(ARG[0]), FUNC(ARG[1]), FUNC(ARG[2]), FUNC(ARG[3]))

#define f32_to_unorm8(val) _mesa_float_to_unorm(val, 8)
#define f32_to_unorm16(val) _mesa_float_to_unorm(val, 16)
#define f32_to_snorm8(val) _mesa_float_to_snorm(val, 8)
#define f32_to_snorm16(val) _mesa_float_to_snorm(val, 16)
#define f32_to_f16(val) _mesa_float_to_half(val)

/**
 * \brief Packs clear color input values into the appropriate accum format.
 *
 * The input value array must have zeroed out elements for components not
 * present in the format. E.g. R8G8B8 has no A component so [3] must be 0.
 *
 * Note: the output is not swizzled so it's packed in RGBA order no matter the
 * component order specified by the vk_format.
 *
 * \param[in] vk_format   Vulkan format of the input color value.
 * \param[in] value       Unpacked RGBA input color values.
 * \param[out] packed_out Accum format packed values.
 */
void pvr_get_hw_clear_color(
   VkFormat vk_format,
   VkClearColorValue value,
   uint32_t packed_out[static const PVR_CLEAR_COLOR_ARRAY_SIZE])
{
   union {
      uint32_t u32[PVR_CLEAR_COLOR_ARRAY_SIZE];
      int32_t i32[PVR_CLEAR_COLOR_ARRAY_SIZE];
      uint16_t u16[PVR_CLEAR_COLOR_ARRAY_SIZE * 2];
      int16_t i16[PVR_CLEAR_COLOR_ARRAY_SIZE * 2];
      uint8_t u8[PVR_CLEAR_COLOR_ARRAY_SIZE * 4];
      int8_t i8[PVR_CLEAR_COLOR_ARRAY_SIZE * 4];
   } packed_val = { 0 };

   /* TODO: Decouple from arch-specific function */
   const enum pvr_pbe_accum_format pbe_accum_format =
      pvr_rogue_get_pbe_accum_format(vk_format);

   static_assert(ARRAY_SIZE(value.uint32) == PVR_CLEAR_COLOR_ARRAY_SIZE,
                 "Size mismatch. Unknown/unhandled extra values.");

   /* TODO: Right now we pack all RGBA values. Would we get any benefit in
    * packing just the components required by the format?
    */

   switch (pbe_accum_format) {
   case PVR_PBE_ACCUM_FORMAT_U8:
      APPLY_FUNC_4V(packed_val.u8, f32_to_unorm8, value.float32);
      break;
   case PVR_PBE_ACCUM_FORMAT_S8:
      APPLY_FUNC_4V(packed_val.i8, f32_to_snorm8, value.float32);
      break;
   case PVR_PBE_ACCUM_FORMAT_UINT8:
      COPY_4V(packed_val.u8, value.uint32);
      break;
   case PVR_PBE_ACCUM_FORMAT_SINT8:
      COPY_4V(packed_val.i8, value.int32);
      break;

   case PVR_PBE_ACCUM_FORMAT_U16:
      APPLY_FUNC_4V(packed_val.u16, f32_to_unorm16, value.float32);
      break;
   case PVR_PBE_ACCUM_FORMAT_S16:
      APPLY_FUNC_4V(packed_val.i16, f32_to_snorm16, value.float32);
      break;
   case PVR_PBE_ACCUM_FORMAT_F16:
      APPLY_FUNC_4V(packed_val.u16, f32_to_f16, value.float32);
      break;
   case PVR_PBE_ACCUM_FORMAT_UINT16:
      COPY_4V(packed_val.u16, value.uint32);
      break;
   case PVR_PBE_ACCUM_FORMAT_SINT16:
      COPY_4V(packed_val.i16, value.int32);
      break;

   case PVR_PBE_ACCUM_FORMAT_U1010102:
      /* The PBE can't handle swizzled 1010102 UINT. */
      packed_val.u32[0] = pvr_pack_a2x10y10z10_uint(
         value.uint32,
         vk_format == VK_FORMAT_A2B10G10R10_UINT_PACK32);
      break;

   case PVR_PBE_ACCUM_FORMAT_F32:
      COPY_4V(packed_val.u32, value.uint32);
      break;
   case PVR_PBE_ACCUM_FORMAT_UINT32:
      COPY_4V(packed_val.u32, value.uint32);
      break;
   case PVR_PBE_ACCUM_FORMAT_SINT32:
      COPY_4V(packed_val.i32, value.int32);
      break;

   default:
      UNREACHABLE("Packing not supported for the accum format.");
      break;
   }

   COPY_4V(packed_out, packed_val.u32);
}

#undef APPLY_FUNC_4V
#undef f32_to_unorm8
#undef f32_to_unorm16
#undef f32_to_snorm8
#undef f32_to_snorm16
#undef f32_to_f16

static inline const struct pvr_format *
pvr_get_format(struct pvr_physical_device *pdevice, VkFormat vk_format)
{
   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   if (format < pdevice->formats.count &&
       pdevice->formats.formats[format].bind != 0) {
      return &pdevice->formats.formats[format];
   }

   mesa_logd("Format %s(%d) not supported\n",
             vk_Format_to_str(vk_format),
             vk_format);

   return NULL;
}

static VkFormatFeatureFlags2
pvr_get_image_format_features2(struct pvr_physical_device *pdevice,
                               VkFormat vk_format,
                               VkImageTiling vk_tiling)
{
   VkFormatFeatureFlags2 flags = 0;

   const struct pvr_format *pvr_format = pvr_get_format(pdevice, vk_format);
   if (!pvr_format)
      return 0;

   assert(pvr_format->bind != 0);

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(vk_format);

   if (pvr_format->bind & PVR_BIND_SAMPLER_VIEW) {
      if (vk_tiling == VK_IMAGE_TILING_OPTIMAL) {
         const uint32_t first_component_size =
            vk_format_get_component_bits(vk_format,
                                         UTIL_FORMAT_COLORSPACE_RGB,
                                         0);

         flags |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
                  VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
                  VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;

         if (!vk_format_is_int(vk_format) &&
             !vk_format_is_depth_or_stencil(vk_format) &&
             (first_component_size < 32 ||
              vk_format_is_block_compressed(vk_format))) {
            flags |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
         }

         if (ycbcr_info) {
            flags |= VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT;
         } else {
            flags |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;
         }
      } else if (!vk_format_is_block_compressed(vk_format)) {
         flags |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
                  VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
                  VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;

         if (ycbcr_info) {
            flags |= VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT;
         } else {
            flags |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;
         }
      }
   }

   if (pvr_format->bind & PVR_BIND_RENDER_TARGET) {
      flags |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
               VK_FORMAT_FEATURE_2_BLIT_DST_BIT;

      if (!vk_format_is_int(vk_format)) {
         flags |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
      }
   }

   if (pvr_format->bind & PVR_BIND_DEPTH_STENCIL) {
      flags |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT |
               VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
               VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
   }

   if (vk_tiling == VK_IMAGE_TILING_OPTIMAL) {
      if (vk_format_is_color(vk_format) &&
          vk_format_get_nr_components(vk_format) == 1 &&
          vk_format_get_blocksizebits(vk_format) == 32 &&
          vk_format_is_int(vk_format)) {
         flags |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;
      }

      if (vk_format_has_stencil(vk_format))
         flags |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;

      if (pvr_format->bind & PVR_BIND_STORAGE_IMAGE)
         flags |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
   }

   if (flags & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) {
      flags |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
               VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
   }

   return flags;
}

const uint8_t *pvr_get_format_swizzle(VkFormat vk_format)
{
   const struct util_format_description *vf = vk_format_description(vk_format);

   return vf->swizzle;
}

/* For DS formats, hardware can only access either depth or stencil at once.
 * It expects to find whichever one it requires in the given context in the
 * first channel, whereas pipe formats swizzle depth into the first channel and
 * stencil into the second.
 */
const uint8_t *
pvr_get_format_swizzle_for_tpu(const struct util_format_description *desc)
{
   const bool has_stencil = util_format_has_stencil(desc);
   const bool has_depth = util_format_has_depth(desc);

   if (has_depth || has_stencil) {
      static const uint8_t pvr_swizzle[4] = {
         PIPE_SWIZZLE_X,
         PIPE_SWIZZLE_NONE,
         PIPE_SWIZZLE_NONE,
         PIPE_SWIZZLE_NONE,
      };

      return pvr_swizzle;
   }

   return desc->swizzle;
}

static VkFormatFeatureFlags2
pvr_get_buffer_format_features2(struct pvr_physical_device *pdevice,
                                VkFormat vk_format)
{
   const struct util_format_description *desc;
   VkFormatFeatureFlags2 flags = 0;

   const struct pvr_format *pvr_format = pvr_get_format(pdevice, vk_format);
   if (!pvr_format)
      return 0;

   assert(pvr_format->bind != 0);

   if (!vk_format_is_color(vk_format))
      return 0;

   desc = vk_format_description(vk_format);

   if (pvr_format->bind & PVR_BIND_VERTEX_BUFFER)
      flags |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   if (vk_format_is_scaled(vk_format))
      return flags;

   flags |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
            VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;

   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN &&
       desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB) {
      if (desc->is_array && vk_format != VK_FORMAT_R32G32B32_UINT &&
          vk_format != VK_FORMAT_R32G32B32_SINT &&
          vk_format != VK_FORMAT_R32G32B32_SFLOAT) {
         flags |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
      } else if (vk_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                 vk_format == VK_FORMAT_A2B10G10R10_UINT_PACK32) {
         flags |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
      }
   }

   if (vk_format_is_color(vk_format) &&
       vk_format_get_nr_components(vk_format) == 1 &&
       vk_format_get_blocksizebits(vk_format) == 32 &&
       vk_format_is_int(vk_format)) {
      flags |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
               VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   switch (vk_format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SNORM:
   case VK_FORMAT_R8G8B8A8_UINT:
   case VK_FORMAT_R8G8B8A8_SINT:
   case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
   case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
   case VK_FORMAT_A8B8G8R8_UINT_PACK32:
   case VK_FORMAT_A8B8G8R8_SINT_PACK32:
   case VK_FORMAT_R16G16B16A16_UINT:
   case VK_FORMAT_R16G16B16A16_SINT:
   case VK_FORMAT_R16G16B16A16_SFLOAT:
   case VK_FORMAT_R32_SFLOAT:
   case VK_FORMAT_R32G32_UINT:
   case VK_FORMAT_R32G32_SINT:
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32B32A32_UINT:
   case VK_FORMAT_R32G32B32A32_SINT:
   case VK_FORMAT_R32G32B32A32_SFLOAT:
      flags |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT;
      break;

   case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      flags |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
      break;

   default:
      break;
   }

   if (flags & VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT) {
      flags |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
               VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
   }

   return flags;
}

static void pvr_get_drm_format_modifier_properties_list(
   struct pvr_physical_device *pdevice,
   VkFormat vk_format,
   VkBaseOutStructure *ext)
{
   assert(ext->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT ||
          ext->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT);

   /* The two top-level data structures are the same.  It's only when
    * you get to walking the actual list of modifier properties that
    * they differ.
    */
   VkDrmFormatModifierPropertiesListEXT *p = (void *)ext;
   const VkFormatFeatureFlags2 linear_features =
      pvr_get_image_format_features2(pdevice, vk_format, VK_IMAGE_TILING_LINEAR);

   /* We support LINEAR only yet */
   if (!linear_features) {
      p->drmFormatModifierCount = 0;
      return;
   }

   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: {
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierPropertiesEXT, out,
                             p->pDrmFormatModifierProperties,
                             &p->drmFormatModifierCount);

      vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out, mp) {
         mp->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
         mp->drmFormatModifierPlaneCount = 1;
         mp->drmFormatModifierTilingFeatures =
            vk_format_features2_to_features(linear_features);
      }
      break;
   }

   case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT: {
      VkDrmFormatModifierPropertiesList2EXT *p2 = (void *)p;
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierProperties2EXT, out,
                             p2->pDrmFormatModifierProperties,
                             &p2->drmFormatModifierCount);

      vk_outarray_append_typed(VkDrmFormatModifierProperties2EXT, &out, mp) {
         mp->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
         mp->drmFormatModifierPlaneCount = 1;
         mp->drmFormatModifierTilingFeatures = linear_features;
      }
      break;
   }

   default:
      UNREACHABLE("Invalid structure type for modifier properties");
   }
}

void pvr_GetPhysicalDeviceFormatProperties2(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkFormatProperties2 *pFormatProperties)
{
   VK_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   VkFormatFeatureFlags2 linear2, optimal2, buffer2;

   linear2 =
      pvr_get_image_format_features2(pdevice, format, VK_IMAGE_TILING_LINEAR);
   optimal2 =
      pvr_get_image_format_features2(pdevice, format, VK_IMAGE_TILING_OPTIMAL);
   buffer2 = pvr_get_buffer_format_features2(pdevice, format);

   pFormatProperties->formatProperties = (VkFormatProperties){
      .linearTilingFeatures = vk_format_features2_to_features(linear2),
      .optimalTilingFeatures = vk_format_features2_to_features(optimal2),
      .bufferFeatures = vk_format_features2_to_features(buffer2),
   };

   vk_foreach_struct (ext, pFormatProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3: {
         VkFormatProperties3 *pFormatProperties3 = (VkFormatProperties3 *)ext;
         pFormatProperties3->linearTilingFeatures = linear2;
         pFormatProperties3->optimalTilingFeatures = optimal2;
         pFormatProperties3->bufferFeatures = buffer2;
         break;
      }
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT:
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT:
         pvr_get_drm_format_modifier_properties_list(pdevice, format, ext);
         break;
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

static VkFormatFeatureFlags2
vk_image_usage_to_format_features(VkImageUsageFlagBits usage_flag)
{
   assert(util_bitcount(usage_flag) == 1);
   switch (usage_flag) {
   case VK_IMAGE_USAGE_TRANSFER_SRC_BIT:
      return VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
             VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   case VK_IMAGE_USAGE_TRANSFER_DST_BIT:
      return VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
             VK_FORMAT_FEATURE_BLIT_DST_BIT;
   case VK_IMAGE_USAGE_SAMPLED_BIT:
      return VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
   case VK_IMAGE_USAGE_STORAGE_BIT:
      return VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
   case VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
   case VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   case VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
             VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   default:
      return 0;
   }
}

static VkResult
pvr_get_image_format_properties(struct pvr_physical_device *pdevice,
                                const VkPhysicalDeviceImageFormatInfo2 *info,
                                VkImageFormatProperties *pImageFormatProperties)
{
   /* Input attachments aren't rendered but they must have the same size
    * restrictions as any framebuffer attachment.
    */
   const VkImageStencilUsageCreateInfo *stencil_usage_info =
      vk_find_struct_const(info->pNext, IMAGE_STENCIL_USAGE_CREATE_INFO);
   const VkImageUsageFlags render_usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
      VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
   const struct pvr_format *pvr_format = pvr_get_format(pdevice, info->format);
   VkFormatFeatureFlags2 tiling_features2;
   VkImageUsageFlags usage =
      info->usage | (stencil_usage_info ? stencil_usage_info->stencilUsage : 0);
   VkImageTiling tiling = info->tiling;
   VkResult result;

   if (!pvr_format) {
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   if (info->flags & VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT) {
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_format_mod_info =
         vk_find_struct_const(info->pNext,
                              PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

      if (drm_format_mod_info &&
          drm_format_mod_info->drmFormatModifier == DRM_FORMAT_MOD_LINEAR) {
         tiling = VK_IMAGE_TILING_LINEAR;
      } else {
         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }
   }

   tiling_features2 =
      pvr_get_image_format_features2(pdevice, info->format, tiling);
   if (tiling_features2 == 0) {
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   /* If VK_IMAGE_CREATE_EXTENDED_USAGE_BIT is set, the driver can't decide if a
    * specific format isn't supported based on the usage.
    */
   if ((info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT) == 0) {
      if (usage & (VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
          !vk_format_is_depth_or_stencil(info->format) &&
          !(pvr_format->bind & PVR_BIND_RENDER_TARGET)) {

         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }

      u_foreach_bit(b, usage) {
         VkFormatFeatureFlags2 usage_features =
            vk_image_usage_to_format_features(1 << b);
         if (usage_features && !(tiling_features2 & usage_features)) {
            result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
            goto err_unsupported_format;
         }
      }
   }

   if (info->type == VK_IMAGE_TYPE_3D) {
      const VkImageUsageFlags transfer_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT;

      /* We don't support 3D depth/stencil images. */
      if (tiling_features2 & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT) {
         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }

      /* Linear tiled 3D images may only be used for transfer or blit
       * operations.
       */
      if (tiling == VK_IMAGE_TILING_LINEAR && usage & ~transfer_usage) {
         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }

      /* Block compressed with 3D layout not supported */
      if (vk_format_is_block_compressed(info->format)) {
         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }
   }

   const uint32_t max_render_size_z =
      rogue_get_render_size_max_z(&pdevice->dev_info);

   if (usage & render_usage) {
      const uint32_t max_render_size =
         rogue_get_render_size_max(&pdevice->dev_info);

      pImageFormatProperties->maxExtent.width = max_render_size;
      pImageFormatProperties->maxExtent.height = max_render_size;
      pImageFormatProperties->maxExtent.depth = max_render_size_z;
   } else {
      const uint32_t max_texture_extent_xy =
         rogue_get_texture_extent_max(&pdevice->dev_info);

      pImageFormatProperties->maxExtent.width = max_texture_extent_xy;
      pImageFormatProperties->maxExtent.height = max_texture_extent_xy;
      pImageFormatProperties->maxExtent.depth = max_render_size_z;
   }

   if (tiling == VK_IMAGE_TILING_LINEAR) {
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->maxArrayLayers = 1;
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   } else {
      /* Default value is the minimum value found in all existing cores. */
      const uint32_t max_multisample =
         PVR_GET_FEATURE_VALUE(&pdevice->dev_info, max_multisample, 4);

      const uint32_t max_sample_bits = ((max_multisample << 1) - 1);

      const uint32_t max_array_layers =
         rogue_get_render_size_max_z(&pdevice->dev_info);

      pImageFormatProperties->maxArrayLayers = max_array_layers;
      pImageFormatProperties->sampleCounts = max_sample_bits;
   }

   if (!(tiling_features2 & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT ||
         tiling_features2 & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   }

   switch (info->type) {
   case VK_IMAGE_TYPE_1D:
      pImageFormatProperties->maxExtent.height = 1;
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
      break;

   case VK_IMAGE_TYPE_2D:
      pImageFormatProperties->maxExtent.depth = 1;

      /* If a 2D image is created to be used in a cube map, then the sample
       * count must be restricted to 1 sample.
       */
      if (info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
         pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;

      break;

   case VK_IMAGE_TYPE_3D:
      pImageFormatProperties->maxArrayLayers = 1;
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
      break;

   default:
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   /* The spec says maxMipLevels may be 1 when tiling is VK_IMAGE_TILING_LINEAR
    * or VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, so for simplicity don't
    * support miplevels for these tilings.
    */
   if (tiling == VK_IMAGE_TILING_LINEAR) {
      pImageFormatProperties->maxMipLevels = 1;
   } else {
      const uint32_t max_size = MAX3(pImageFormatProperties->maxExtent.width,
                                     pImageFormatProperties->maxExtent.height,
                                     pImageFormatProperties->maxExtent.depth);

      pImageFormatProperties->maxMipLevels = util_logbase2(max_size) + 1U;
   }

   /* Return 2GB (minimum required from spec).
    *
    * From the Vulkan spec:
    *
    *    maxResourceSize is an upper bound on the total image size in bytes,
    *    inclusive of all image subresources. Implementations may have an
    *    address space limit on total size of a resource, which is advertised by
    *    this property. maxResourceSize must be at least 2^31.
    */
   pImageFormatProperties->maxResourceSize = 2ULL * 1024 * 1024 * 1024;

   return VK_SUCCESS;

err_unsupported_format:
   /* From the Vulkan 1.0.42 spec:
    *
    *    If the combination of parameters to
    *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by the
    *    implementation for use in vkCreateImage, then all members of
    *    imageFormatProperties will be filled with zero.
    */
   *pImageFormatProperties = (VkImageFormatProperties){ 0 };

   return result;
}

/* FIXME: Should this be returning VK_ERROR_FORMAT_NOT_SUPPORTED when tiling is
 * linear and the image type is 3D or flags contains
 * VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT? This should avoid well behaved apps
 * attempting to create invalid image views, as pvr_pack_tex_state() will return
 * VK_ERROR_FORMAT_NOT_SUPPORTED in these cases.
 */
VkResult pvr_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   VK_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   VkExternalImageFormatProperties *external_props = NULL;
   VkResult result = pvr_get_image_format_properties(
      pdevice,
      pImageFormatInfo,
      &pImageFormatProperties->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const (ext, pImageFormatInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)ext;
         break;
      case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
         break;
      case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO:
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT:
         /* Nothing to do here, it's handled in
          * pvr_get_image_format_properties)
          */
         break;
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct (ext, pImageFormatProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)ext;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: {
         VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props =
            (void *)ext;
         ycbcr_props->combinedImageSamplerDescriptorCount = 1;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
         if (!external_props)
            break;

         external_props->externalMemoryProperties.externalMemoryFeatures =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         external_props->externalMemoryProperties.compatibleHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         external_props->externalMemoryProperties.exportFromImportedHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         return vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      }
   }

   return VK_SUCCESS;
}

void pvr_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkSampleCountFlagBits samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pNumProperties,
   VkSparseImageFormatProperties *pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
}

void pvr_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}

void pvr_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   /* The Vulkan 1.0.42 spec says "handleType must be a valid
    * VkExternalMemoryHandleTypeFlagBits value" in
    * VkPhysicalDeviceExternalBufferInfo. This differs from
    * VkPhysicalDeviceExternalImageFormatInfo, which surprisingly permits
    * handleType == 0.
    */
   assert(pExternalBufferInfo->handleType != 0);

   /* All of the current flags are for sparse which we don't support. */
   if (pExternalBufferInfo->flags)
      goto unsupported;

   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      /* clang-format off */
      pExternalBufferProperties->externalMemoryProperties.externalMemoryFeatures =
         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      pExternalBufferProperties->externalMemoryProperties.exportFromImportedHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      pExternalBufferProperties->externalMemoryProperties.compatibleHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      /* clang-format on */
      return;
   default:
      break;
   }

unsupported:
   /* From the Vulkan 1.1.113 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties){
         .compatibleHandleTypes = pExternalBufferInfo->handleType,
      };
}

uint32_t pvr_pbe_pixel_num_loads(enum pvr_transfer_pbe_pixel_src pbe_format)
{
   switch (pbe_format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16F16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X2:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X4:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW64:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW128:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S8D24:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D32S8:

   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_UV_INTERLEAVED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_U_V:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YUV_PACKED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YVU_PACKED:
      return 1U;

   case PVR_TRANSFER_PBE_PIXEL_SRC_NUM:
   default:
      return 0U;
   }
}

bool pvr_pbe_pixel_is_norm(enum pvr_transfer_pbe_pixel_src pbe_format)
{
   switch (pbe_format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_U_V:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YUV_PACKED:

   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_UV_INTERLEAVED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YVU_PACKED:

   case PVR_TRANSFER_PBE_PIXEL_SRC_F16F16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X2:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X4:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
      return true;

   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW64:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW128:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S8D24:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK48:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK64:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK96:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK128:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
      return false;

   default:
      UNREACHABLE("Invalid pvr_transfer_pbe_pixel_src");
   }

   return false;
}

uint32_t pvr_pbe_pixel_size(enum pvr_transfer_pbe_pixel_src pbe_format)
{
   switch (pbe_format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:

   case PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8:

   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:

   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW32:

   case PVR_TRANSFER_PBE_PIXEL_SRC_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S8D24:

   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:

   case PVR_TRANSFER_PBE_PIXEL_SRC_YUV_PACKED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YVU_PACKED:

   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK32:
      return 1;

   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:

   case PVR_TRANSFER_PBE_PIXEL_SRC_F16F16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S16NORM:

   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:

   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X2:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW64:

   case PVR_TRANSFER_PBE_PIXEL_SRC_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:

   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_U_V:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_UV_INTERLEAVED:

   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK48:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK64:
      return 2;

   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK96:
      return 3;

   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:

   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X4:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW128:

   case PVR_TRANSFER_PBE_PIXEL_SRC_MASK128:
      return 4;

   default:
      UNREACHABLE("Invalid pvr_transfer_pbe_pixel_src");
   }

   return 0;
}

unsigned pvr_pbe_format_num_sample_components(enum pvr_transfer_pbe_pixel_src format)
{
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_UV_INTERLEAVED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YVU_PACKED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_U_V:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YUV_PACKED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S8D24:
      return 1;

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW64:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X2:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D32S8:
      return 2;

   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16F16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S16NORM:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW128:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X4:
      return 4;

   default:
      UNREACHABLE("Unsupported transfer PBE pixel format");
      return 4;
   }
}
