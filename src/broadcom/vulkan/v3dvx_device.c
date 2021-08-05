/*
 * Copyright © 2021 Raspberry Pi
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

#include "v3dv_private.h"

#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"
#include "broadcom/compiler/v3d_compiler.h"
#include "vk_format_info.h"
#include "util/u_pack_color.h"

static const enum V3DX(Wrap_Mode) vk_to_v3d_wrap_mode[] = {
   [VK_SAMPLER_ADDRESS_MODE_REPEAT]          = V3D_WRAP_MODE_REPEAT,
   [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT] = V3D_WRAP_MODE_MIRROR,
   [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE]   = V3D_WRAP_MODE_CLAMP,
   [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE] = V3D_WRAP_MODE_MIRROR_ONCE,
   [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER] = V3D_WRAP_MODE_BORDER,
};

static const enum V3DX(Compare_Function)
vk_to_v3d_compare_func[] = {
   [VK_COMPARE_OP_NEVER]                        = V3D_COMPARE_FUNC_NEVER,
   [VK_COMPARE_OP_LESS]                         = V3D_COMPARE_FUNC_LESS,
   [VK_COMPARE_OP_EQUAL]                        = V3D_COMPARE_FUNC_EQUAL,
   [VK_COMPARE_OP_LESS_OR_EQUAL]                = V3D_COMPARE_FUNC_LEQUAL,
   [VK_COMPARE_OP_GREATER]                      = V3D_COMPARE_FUNC_GREATER,
   [VK_COMPARE_OP_NOT_EQUAL]                    = V3D_COMPARE_FUNC_NOTEQUAL,
   [VK_COMPARE_OP_GREATER_OR_EQUAL]             = V3D_COMPARE_FUNC_GEQUAL,
   [VK_COMPARE_OP_ALWAYS]                       = V3D_COMPARE_FUNC_ALWAYS,
};

void
v3dX(pack_sampler_state)(struct v3dv_sampler *sampler,
                         const VkSamplerCreateInfo *pCreateInfo)
{
   enum V3DX(Border_Color_Mode) border_color_mode;

   /* For now we only support the preset Vulkan border color modes. If we
    * want to implement VK_EXT_custom_border_color in the future we would have
    * to use V3D_BORDER_COLOR_FOLLOWS, and fill up border_color_word_[0/1/2/3]
    * SAMPLER_STATE.
    */
   switch (pCreateInfo->borderColor) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      border_color_mode = V3D_BORDER_COLOR_0000;
      break;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      border_color_mode = V3D_BORDER_COLOR_0001;
      break;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      border_color_mode = V3D_BORDER_COLOR_1111;
      break;
   default:
      unreachable("Unknown border color");
      break;
   }

   /* For some texture formats, when clamping to transparent black border the
    * CTS expects alpha to be set to 1 instead of 0, but the border color mode
    * will take priority over the texture state swizzle, so the only way to
    * fix that is to apply a swizzle in the shader. Here we keep track of
    * whether we are activating that mode and we will decide if we need to
    * activate the texture swizzle lowering in the shader key at compile time
    * depending on the actual texture format.
    */
   if ((pCreateInfo->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        pCreateInfo->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        pCreateInfo->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) &&
       border_color_mode == V3D_BORDER_COLOR_0000) {
      sampler->clamp_to_transparent_black_border = true;
   }

   v3dvx_pack(sampler->sampler_state, SAMPLER_STATE, s) {
      if (pCreateInfo->anisotropyEnable) {
         s.anisotropy_enable = true;
         if (pCreateInfo->maxAnisotropy > 8)
            s.maximum_anisotropy = 3;
         else if (pCreateInfo->maxAnisotropy > 4)
            s.maximum_anisotropy = 2;
         else if (pCreateInfo->maxAnisotropy > 2)
            s.maximum_anisotropy = 1;
      }

      s.border_color_mode = border_color_mode;

      s.wrap_i_border = false; /* Also hardcoded on v3d */
      s.wrap_s = vk_to_v3d_wrap_mode[pCreateInfo->addressModeU];
      s.wrap_t = vk_to_v3d_wrap_mode[pCreateInfo->addressModeV];
      s.wrap_r = vk_to_v3d_wrap_mode[pCreateInfo->addressModeW];
      s.fixed_bias = pCreateInfo->mipLodBias;
      s.max_level_of_detail = MIN2(MAX2(0, pCreateInfo->maxLod), 15);
      s.min_level_of_detail = MIN2(MAX2(0, pCreateInfo->minLod), 15);
      s.srgb_disable = 0; /* Not even set by v3d */
      s.depth_compare_function =
         vk_to_v3d_compare_func[pCreateInfo->compareEnable ?
                                pCreateInfo->compareOp : VK_COMPARE_OP_NEVER];
      s.mip_filter_nearest = pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST;
      s.min_filter_nearest = pCreateInfo->minFilter == VK_FILTER_NEAREST;
      s.mag_filter_nearest = pCreateInfo->magFilter == VK_FILTER_NEAREST;
   }
}

/**
 * This computes the maximum bpp used by any of the render targets used by
 * a particular subpass and checks if any of those render targets are
 * multisampled. If we don't have a subpass (when we are not inside a
 * render pass), then we assume that all framebuffer attachments are used.
 */
void
v3dX(framebuffer_compute_internal_bpp_msaa)(
   const struct v3dv_framebuffer *framebuffer,
   const struct v3dv_subpass *subpass,
   uint8_t *max_bpp,
   bool *msaa)
{
   STATIC_ASSERT(RENDER_TARGET_MAXIMUM_32BPP == 0);
   *max_bpp = RENDER_TARGET_MAXIMUM_32BPP;
   *msaa = false;

   if (subpass) {
      for (uint32_t i = 0; i < subpass->color_count; i++) {
         uint32_t att_idx = subpass->color_attachments[i].attachment;
         if (att_idx == VK_ATTACHMENT_UNUSED)
            continue;

         const struct v3dv_image_view *att = framebuffer->attachments[att_idx];
         assert(att);

         if (att->aspects & VK_IMAGE_ASPECT_COLOR_BIT)
            *max_bpp = MAX2(*max_bpp, att->internal_bpp);

         if (att->image->samples > VK_SAMPLE_COUNT_1_BIT)
            *msaa = true;
      }

      if (!*msaa && subpass->ds_attachment.attachment != VK_ATTACHMENT_UNUSED) {
         const struct v3dv_image_view *att =
            framebuffer->attachments[subpass->ds_attachment.attachment];
         assert(att);

         if (att->image->samples > VK_SAMPLE_COUNT_1_BIT)
            *msaa = true;
      }

      return;
   }

   assert(framebuffer->attachment_count <= 4);
   for (uint32_t i = 0; i < framebuffer->attachment_count; i++) {
      const struct v3dv_image_view *att = framebuffer->attachments[i];
      assert(att);

      if (att->aspects & VK_IMAGE_ASPECT_COLOR_BIT)
         *max_bpp = MAX2(*max_bpp, att->internal_bpp);

      if (att->image->samples > VK_SAMPLE_COUNT_1_BIT)
         *msaa = true;
   }

   return;
}

uint32_t
v3dX(zs_buffer_from_aspect_bits)(VkImageAspectFlags aspects)
{
   const VkImageAspectFlags zs_aspects =
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
   const VkImageAspectFlags filtered_aspects = aspects & zs_aspects;

   if (filtered_aspects == zs_aspects)
      return ZSTENCIL;
   else if (filtered_aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
      return Z;
   else if (filtered_aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
      return STENCIL;
   else
      return NONE;
}

void
v3dX(get_hw_clear_color)(const VkClearColorValue *color,
                         uint32_t internal_type,
                         uint32_t internal_size,
                         uint32_t *hw_color)
{
   union util_color uc;
   switch (internal_type) {
   case V3D_INTERNAL_TYPE_8:
      util_pack_color(color->float32, PIPE_FORMAT_R8G8B8A8_UNORM, &uc);
      memcpy(hw_color, uc.ui, internal_size);
   break;
   case V3D_INTERNAL_TYPE_8I:
   case V3D_INTERNAL_TYPE_8UI:
      hw_color[0] = ((color->uint32[0] & 0xff) |
                     (color->uint32[1] & 0xff) << 8 |
                     (color->uint32[2] & 0xff) << 16 |
                     (color->uint32[3] & 0xff) << 24);
   break;
   case V3D_INTERNAL_TYPE_16F:
      util_pack_color(color->float32, PIPE_FORMAT_R16G16B16A16_FLOAT, &uc);
      memcpy(hw_color, uc.ui, internal_size);
   break;
   case V3D_INTERNAL_TYPE_16I:
   case V3D_INTERNAL_TYPE_16UI:
      hw_color[0] = ((color->uint32[0] & 0xffff) | color->uint32[1] << 16);
      hw_color[1] = ((color->uint32[2] & 0xffff) | color->uint32[3] << 16);
   break;
   case V3D_INTERNAL_TYPE_32F:
   case V3D_INTERNAL_TYPE_32I:
   case V3D_INTERNAL_TYPE_32UI:
      memcpy(hw_color, color->uint32, internal_size);
      break;
   }
}

#ifdef DEBUG
void
v3dX(device_check_prepacked_sizes)(void)
{
   STATIC_ASSERT(V3DV_SAMPLER_STATE_LENGTH >=
                 cl_packet_length(SAMPLER_STATE));
   STATIC_ASSERT(V3DV_TEXTURE_SHADER_STATE_LENGTH >=
                 cl_packet_length(TEXTURE_SHADER_STATE));
   STATIC_ASSERT(V3DV_SAMPLER_STATE_LENGTH >=
                 cl_packet_length(SAMPLER_STATE));
   STATIC_ASSERT(V3DV_BLEND_CFG_LENGTH>=
                 cl_packet_length(BLEND_CFG));
   STATIC_ASSERT(V3DV_CFG_BITS_LENGTH>=
                 cl_packet_length(CFG_BITS));
   STATIC_ASSERT(V3DV_GL_SHADER_STATE_RECORD_LENGTH >=
                 cl_packet_length(GL_SHADER_STATE_RECORD));
   STATIC_ASSERT(V3DV_VCM_CACHE_SIZE_LENGTH>=
                 cl_packet_length(VCM_CACHE_SIZE));
   STATIC_ASSERT(V3DV_GL_SHADER_STATE_ATTRIBUTE_RECORD_LENGTH >=
                 cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD));
   STATIC_ASSERT(V3DV_STENCIL_CFG_LENGTH >=
                 cl_packet_length(STENCIL_CFG));
}
#endif
