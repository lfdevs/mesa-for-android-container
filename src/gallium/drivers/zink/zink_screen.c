/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_screen.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_fence.h"
#include "zink_public.h"
#include "zink_resource.h"

#include "os/os_process.h"
#include "util/u_debug.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"

#include "state_tracker/sw_winsys.h"

static const struct debug_named_value
debug_options[] = {
   { "nir", ZINK_DEBUG_NIR, "Dump NIR during program compile" },
   { "spirv", ZINK_DEBUG_SPIRV, "Dump SPIR-V during program compile" },
   { "tgsi", ZINK_DEBUG_TGSI, "Dump TGSI during program compile" },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(zink_debug, "ZINK_DEBUG", debug_options, 0)

uint32_t
zink_debug;

static const char *
zink_get_vendor(struct pipe_screen *pscreen)
{
   return "Collabora Ltd";
}

static const char *
zink_get_device_vendor(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   static char buf[1000];
   snprintf(buf, sizeof(buf), "Unknown (vendor-id: 0x%04x)", screen->props.vendorID);
   return buf;
}

static const char *
zink_get_name(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   static char buf[1000];
   snprintf(buf, sizeof(buf), "zink (%s)", screen->props.deviceName);
   return buf;
}

static int
get_video_mem(struct zink_screen *screen)
{
   VkDeviceSize size = 0;
   for (uint32_t i = 0; i < screen->mem_props.memoryHeapCount; ++i)
      size += screen->mem_props.memoryHeaps[i].size;
   return (int)(size >> 20);
}

static int
zink_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_CAP_NPOT_TEXTURES:
      return 1;

   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
      return screen->props.limits.maxFragmentDualSrcAttachments;

   case PIPE_CAP_POINT_SPRITE:
      return 1;

   case PIPE_CAP_MAX_RENDER_TARGETS:
      return screen->props.limits.maxColorAttachments;

   case PIPE_CAP_OCCLUSION_QUERY:
      return 1;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_QUERY_TIME_ELAPSED:
      return 1;
#endif

   case PIPE_CAP_TEXTURE_SWIZZLE:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      return screen->props.limits.maxImageDimension2D;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return 1 + util_logbase2(screen->props.limits.maxImageDimension3D);
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return 1 + util_logbase2(screen->props.limits.maxImageDimensionCube);

   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
      return 1;

   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
      return 0; /* TODO: re-enable after implementing nir_texop_txd */

   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
      return 1;

   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
      return 1;

   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return screen->props.limits.maxImageArrayLayers;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
      return 0;
#endif

#if 0 /* TODO: Enable me */
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
      return 1;
#endif

   case PIPE_CAP_SEAMLESS_CUBE_MAP:
      return 1;

   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return screen->props.limits.minTexelOffset;
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return screen->props.limits.maxTexelOffset;

   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
      return 1;

   case PIPE_CAP_GLSL_FEATURE_LEVEL:
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 120;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_COMPUTE:
      return 1;
#endif

   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return screen->props.limits.minUniformBufferOffsetAlignment;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_QUERY_TIMESTAMP:
      return 1;
#endif

   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return screen->props.limits.minMemoryMapAlignment;

   case PIPE_CAP_CUBE_MAP_ARRAY:
      return screen->feats.imageCubeArray;

   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
      return 0; /* unsure */

   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return screen->props.limits.maxTexelBufferElements;

   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_NATIVE; /* unsure */

   case PIPE_CAP_MAX_VIEWPORTS:
      return screen->props.limits.maxViewports;

   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
      return 1;

   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
      return screen->props.limits.maxGeometryOutputVertices;
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return screen->props.limits.maxGeometryOutputComponents;

#if 0 /* TODO: Enable me. Enables ARB_texture_gather */
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
      return 4;
#endif

   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
      return screen->props.limits.minTexelGatherOffset;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
      return screen->props.limits.maxTexelGatherOffset;

   case PIPE_CAP_VENDOR_ID:
      return screen->props.vendorID;
   case PIPE_CAP_DEVICE_ID:
      return screen->props.deviceID;

   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_VIDEO_MEMORY:
      return get_video_mem(screen);
   case PIPE_CAP_UMA:
      return screen->props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return screen->props.limits.maxVertexInputBindingStride;

#if 0 /* TODO: Enable me */
   case PIPE_CAP_SAMPLER_VIEW_TARGET:
      return 1;
#endif

#if 0 /* TODO: Enable me */
   case PIPE_CAP_CLIP_HALFZ:
      return 1;
#endif

#if 0 /* TODO: Enable me */
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
      return 1;
#endif

   case PIPE_CAP_SHAREABLE_SHADERS:
      return 1;

#if 0 /* TODO: Enable me. Enables GL_ARB_shader_storage_buffer_object */
   case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
      return screen->props.limits.minStorageBufferOffsetAlignment;
#endif

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0; /* TODO: figure these out */

#if 0 /* TODO: Enable me */
   case PIPE_CAP_CULL_DISTANCE:
      return screen->feats.shaderCullDistance;
#endif

   case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
      return screen->props.limits.viewportSubPixelBits;

   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
      return 0; /* not sure */

   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return 0; /* not implemented */

   case PIPE_CAP_MAX_COMBINED_SHADER_BUFFERS:
      return screen->props.limits.maxDescriptorSetStorageBuffers;

   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return screen->props.limits.maxStorageBufferRange; /* unsure */

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
      return 1;

   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
      return 0;

   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
      return 0;

   case PIPE_CAP_NIR_COMPACT_ARRAYS:
      return 1;

   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
      return 1;

   case PIPE_CAP_FLATSHADE:
   case PIPE_CAP_ALPHA_TEST:
   case PIPE_CAP_CLIP_PLANES:
   case PIPE_CAP_POINT_SIZE_FIXED:
   case PIPE_CAP_TWO_SIDED_COLOR:
      return 0;

   case PIPE_CAP_DMABUF:
      return screen->have_KHR_external_memory_fd;

   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
zink_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
      return screen->props.limits.lineWidthRange[1];

   case PIPE_CAPF_MAX_POINT_WIDTH:
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return screen->props.limits.pointSizeRange[1];

   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return screen->props.limits.maxSamplerAnisotropy;

   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return screen->props.limits.maxSamplerLodBias;

   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f; /* not implemented */
   }

   /* should only get here on unhandled cases */
   return 0.0;
}

static int
zink_get_shader_param(struct pipe_screen *pscreen,
                       enum pipe_shader_type shader,
                       enum pipe_shader_cap param)
{
   struct zink_screen *screen = zink_screen(pscreen);

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      if (shader == PIPE_SHADER_VERTEX ||
          shader == PIPE_SHADER_FRAGMENT)
         return INT_MAX;
      return 0;

   case PIPE_SHADER_CAP_MAX_INPUTS:
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         return MIN2(screen->props.limits.maxVertexInputAttributes,
                     PIPE_MAX_SHADER_INPUTS);
      case PIPE_SHADER_FRAGMENT:
         return MIN2(screen->props.limits.maxFragmentInputComponents / 4,
                     PIPE_MAX_SHADER_INPUTS);
      default:
         return 0; /* unsupported stage */
      }

   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      switch (shader) {
      case PIPE_SHADER_VERTEX:
         return MIN2(screen->props.limits.maxVertexOutputComponents / 4,
                     PIPE_MAX_SHADER_OUTPUTS);
      case PIPE_SHADER_FRAGMENT:
         return MIN2(screen->props.limits.maxColorAttachments,
                PIPE_MAX_SHADER_OUTPUTS);
      default:
         return 0; /* unsupported stage */
      }

   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      /* this might be a bit simplistic... */
      return MIN2(screen->props.limits.maxPerStageDescriptorSamplers,
                  PIPE_MAX_SAMPLERS);

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      return MIN2(screen->props.limits.maxUniformBufferRange, INT_MAX);

   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return screen->props.limits.maxPerStageDescriptorUniformBuffers;

   case PIPE_SHADER_CAP_MAX_TEMPS:
      return INT_MAX;

   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
   case PIPE_SHADER_CAP_SUBROUTINES:
   case PIPE_SHADER_CAP_INTEGERS:
   case PIPE_SHADER_CAP_INT64_ATOMICS:
   case PIPE_SHADER_CAP_FP16:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_PREFERRED_IR:
      return PIPE_SHADER_IR_NIR;

   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return MIN2(screen->props.limits.maxPerStageDescriptorSampledImages,
                  PIPE_MAX_SHADER_SAMPLER_VIEWS);

   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
      return 0; /* not implemented */

   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      return 0; /* no idea */

   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 32; /* arbitrary */

   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      /* TODO: this limitation is dumb, and will need some fixes in mesa */
      return MIN2(screen->props.limits.maxPerStageDescriptorStorageBuffers, 8);

   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);

   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      return MIN2(screen->props.limits.maxPerStageDescriptorStorageImages,
                  PIPE_MAX_SHADER_IMAGES);

   case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
   case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
      return 0; /* unsure */

   case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      return 0; /* not implemented */
   }

   /* should only get here on unhandled cases */
   return 0;
}

static const VkFormat formats[PIPE_FORMAT_COUNT] = {
#define MAP_FORMAT_NORM(FMT) \
   [PIPE_FORMAT_ ## FMT ## _UNORM] = VK_FORMAT_ ## FMT ## _UNORM, \
   [PIPE_FORMAT_ ## FMT ## _SNORM] = VK_FORMAT_ ## FMT ## _SNORM,

#define MAP_FORMAT_SCALED(FMT) \
   [PIPE_FORMAT_ ## FMT ## _USCALED] = VK_FORMAT_ ## FMT ## _USCALED, \
   [PIPE_FORMAT_ ## FMT ## _SSCALED] = VK_FORMAT_ ## FMT ## _SSCALED,

#define MAP_FORMAT_INT(FMT) \
   [PIPE_FORMAT_ ## FMT ## _UINT] = VK_FORMAT_ ## FMT ## _UINT, \
   [PIPE_FORMAT_ ## FMT ## _SINT] = VK_FORMAT_ ## FMT ## _SINT,

#define MAP_FORMAT_SRGB(FMT) \
   [PIPE_FORMAT_ ## FMT ## _SRGB] = VK_FORMAT_ ## FMT ## _SRGB,

#define MAP_FORMAT_FLOAT(FMT) \
   [PIPE_FORMAT_ ## FMT ## _FLOAT] = VK_FORMAT_ ## FMT ## _SFLOAT,

   // one component

   // 8-bits
   MAP_FORMAT_NORM(R8)
   MAP_FORMAT_SCALED(R8)
   MAP_FORMAT_INT(R8)
   // 16-bits
   MAP_FORMAT_NORM(R16)
   MAP_FORMAT_SCALED(R16)
   MAP_FORMAT_INT(R16)
   MAP_FORMAT_FLOAT(R16)
   // 32-bits
   MAP_FORMAT_INT(R32)
   MAP_FORMAT_FLOAT(R32)

   // two components

   // 8-bits
   MAP_FORMAT_NORM(R8G8)
   MAP_FORMAT_SCALED(R8G8)
   MAP_FORMAT_INT(R8G8)
   // 16-bits
   MAP_FORMAT_NORM(R16G16)
   MAP_FORMAT_SCALED(R16G16)
   MAP_FORMAT_INT(R16G16)
   MAP_FORMAT_FLOAT(R16G16)
   // 32-bits
   MAP_FORMAT_INT(R32G32)
   MAP_FORMAT_FLOAT(R32G32)

   // three components

   // 8-bits
   MAP_FORMAT_NORM(R8G8B8)
   MAP_FORMAT_SCALED(R8G8B8)
   MAP_FORMAT_INT(R8G8B8)
   MAP_FORMAT_SRGB(R8G8B8)
   // 16-bits
   MAP_FORMAT_NORM(R16G16B16)
   MAP_FORMAT_SCALED(R16G16B16)
   MAP_FORMAT_INT(R16G16B16)
   MAP_FORMAT_FLOAT(R16G16B16)
   // 32-bits
   MAP_FORMAT_INT(R32G32B32)
   MAP_FORMAT_FLOAT(R32G32B32)

   // four components

   // 8-bits
   MAP_FORMAT_NORM(R8G8B8A8)
   MAP_FORMAT_SCALED(R8G8B8A8)
   MAP_FORMAT_INT(R8G8B8A8)
   MAP_FORMAT_SRGB(R8G8B8A8)
   [PIPE_FORMAT_B8G8R8A8_UNORM] = VK_FORMAT_B8G8R8A8_UNORM,
   [PIPE_FORMAT_B8G8R8X8_UNORM] = VK_FORMAT_B8G8R8A8_UNORM,
   MAP_FORMAT_SRGB(B8G8R8A8)
   [PIPE_FORMAT_A8B8G8R8_SRGB] = VK_FORMAT_A8B8G8R8_SRGB_PACK32,
   // 16-bits
   MAP_FORMAT_NORM(R16G16B16A16)
   MAP_FORMAT_SCALED(R16G16B16A16)
   MAP_FORMAT_INT(R16G16B16A16)
   MAP_FORMAT_FLOAT(R16G16B16A16)
   // 32-bits
   MAP_FORMAT_INT(R32G32B32A32)
   MAP_FORMAT_FLOAT(R32G32B32A32)

   // other color formats
   [PIPE_FORMAT_B5G6R5_UNORM] = VK_FORMAT_R5G6B5_UNORM_PACK16,
   [PIPE_FORMAT_B5G5R5A1_UNORM] = VK_FORMAT_B5G5R5A1_UNORM_PACK16,
   [PIPE_FORMAT_R11G11B10_FLOAT] = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
   [PIPE_FORMAT_R9G9B9E5_FLOAT] = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
   [PIPE_FORMAT_R10G10B10A2_UNORM] = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
   [PIPE_FORMAT_B10G10R10A2_UNORM] = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
   [PIPE_FORMAT_R10G10B10A2_UINT] = VK_FORMAT_A2B10G10R10_UINT_PACK32,
   [PIPE_FORMAT_B10G10R10A2_UINT] = VK_FORMAT_A2R10G10B10_UINT_PACK32,

   // depth/stencil formats
   [PIPE_FORMAT_Z32_FLOAT] = VK_FORMAT_D32_SFLOAT,
   [PIPE_FORMAT_Z32_FLOAT_S8X24_UINT] = VK_FORMAT_D32_SFLOAT_S8_UINT,
   [PIPE_FORMAT_Z16_UNORM] = VK_FORMAT_D16_UNORM,
   [PIPE_FORMAT_Z24X8_UNORM] = VK_FORMAT_X8_D24_UNORM_PACK32,
   [PIPE_FORMAT_Z24_UNORM_S8_UINT] = VK_FORMAT_D24_UNORM_S8_UINT,

   // compressed formats
   [PIPE_FORMAT_DXT1_RGB] = VK_FORMAT_BC1_RGB_UNORM_BLOCK,
   [PIPE_FORMAT_DXT1_RGBA] = VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
   [PIPE_FORMAT_DXT3_RGBA] = VK_FORMAT_BC2_UNORM_BLOCK,
   [PIPE_FORMAT_DXT5_RGBA] = VK_FORMAT_BC3_UNORM_BLOCK,
   [PIPE_FORMAT_DXT1_SRGB] = VK_FORMAT_BC1_RGB_SRGB_BLOCK,
   [PIPE_FORMAT_DXT1_SRGBA] = VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
   [PIPE_FORMAT_DXT3_SRGBA] = VK_FORMAT_BC2_SRGB_BLOCK,
   [PIPE_FORMAT_DXT5_SRGBA] = VK_FORMAT_BC3_SRGB_BLOCK,

   [PIPE_FORMAT_RGTC1_UNORM] = VK_FORMAT_BC4_UNORM_BLOCK,
   [PIPE_FORMAT_RGTC1_SNORM] = VK_FORMAT_BC4_SNORM_BLOCK,
   [PIPE_FORMAT_RGTC2_UNORM] = VK_FORMAT_BC5_UNORM_BLOCK,
   [PIPE_FORMAT_RGTC2_SNORM] = VK_FORMAT_BC5_SNORM_BLOCK,
   [PIPE_FORMAT_BPTC_RGBA_UNORM] = VK_FORMAT_BC7_UNORM_BLOCK,
   [PIPE_FORMAT_BPTC_SRGBA] = VK_FORMAT_BC7_SRGB_BLOCK,
   [PIPE_FORMAT_BPTC_RGB_FLOAT] = VK_FORMAT_BC6H_SFLOAT_BLOCK,
   [PIPE_FORMAT_BPTC_RGB_UFLOAT] = VK_FORMAT_BC6H_UFLOAT_BLOCK,
};

static bool
is_depth_format_supported(struct zink_screen *screen, VkFormat format)
{
   VkFormatProperties props;
   vkGetPhysicalDeviceFormatProperties(screen->pdev, format, &props);
   return (props.linearTilingFeatures | props.optimalTilingFeatures) &
          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
}

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format)
{
   VkFormat ret = formats[format];

   if (ret == VK_FORMAT_X8_D24_UNORM_PACK32 &&
       !screen->have_X8_D24_UNORM_PACK32) {
      assert(is_depth_format_supported(screen, VK_FORMAT_D32_SFLOAT));
      return VK_FORMAT_D32_SFLOAT;
   }

   if (ret == VK_FORMAT_D24_UNORM_S8_UINT &&
       !screen->have_D24_UNORM_S8_UINT) {
      assert(is_depth_format_supported(screen, VK_FORMAT_D32_SFLOAT_S8_UINT));
      return VK_FORMAT_D32_SFLOAT_S8_UINT;
   }

   return ret;
}

static VkSampleCountFlagBits
vk_sample_count_flags(uint32_t sample_count)
{
   switch (sample_count) {
   case 1: return VK_SAMPLE_COUNT_1_BIT;
   case 2: return VK_SAMPLE_COUNT_2_BIT;
   case 4: return VK_SAMPLE_COUNT_4_BIT;
   case 8: return VK_SAMPLE_COUNT_8_BIT;
   case 16: return VK_SAMPLE_COUNT_16_BIT;
   case 32: return VK_SAMPLE_COUNT_32_BIT;
   case 64: return VK_SAMPLE_COUNT_64_BIT;
   default:
      return 0;
   }
}

static bool
zink_is_format_supported(struct pipe_screen *pscreen,
                         enum pipe_format format,
                         enum pipe_texture_target target,
                         unsigned sample_count,
                         unsigned storage_sample_count,
                         unsigned bind)
{
   struct zink_screen *screen = zink_screen(pscreen);

   if (format == PIPE_FORMAT_NONE)
      return screen->props.limits.framebufferNoAttachmentsSampleCounts &
             vk_sample_count_flags(sample_count);

   VkFormat vkformat = zink_get_format(screen, format);
   if (vkformat == VK_FORMAT_UNDEFINED)
      return FALSE;

   if (sample_count >= 1) {
      VkSampleCountFlagBits sample_mask = vk_sample_count_flags(sample_count);
      const struct util_format_description *desc = util_format_description(format);
      if (util_format_is_depth_or_stencil(format)) {
         if (util_format_has_depth(desc)) {
            if (bind & PIPE_BIND_DEPTH_STENCIL &&
                (screen->props.limits.framebufferDepthSampleCounts & sample_mask) != sample_mask)
               return FALSE;
            if (bind & PIPE_BIND_SAMPLER_VIEW &&
                (screen->props.limits.sampledImageDepthSampleCounts & sample_mask) != sample_mask)
               return FALSE;
         }
         if (util_format_has_stencil(desc)) {
            if (bind & PIPE_BIND_DEPTH_STENCIL &&
                (screen->props.limits.framebufferStencilSampleCounts & sample_mask) != sample_mask)
               return FALSE;
            if (bind & PIPE_BIND_SAMPLER_VIEW &&
                (screen->props.limits.sampledImageStencilSampleCounts & sample_mask) != sample_mask)
               return FALSE;
         }
      } else if (util_format_is_pure_integer(format)) {
         if (bind & PIPE_BIND_RENDER_TARGET &&
             !(screen->props.limits.framebufferColorSampleCounts & sample_mask))
            return FALSE;
         if (bind & PIPE_BIND_SAMPLER_VIEW &&
             !(screen->props.limits.sampledImageIntegerSampleCounts & sample_mask))
            return FALSE;
      } else {
         if (bind & PIPE_BIND_RENDER_TARGET &&
             !(screen->props.limits.framebufferColorSampleCounts & sample_mask))
            return FALSE;
         if (bind & PIPE_BIND_SAMPLER_VIEW &&
             !(screen->props.limits.sampledImageColorSampleCounts & sample_mask))
            return FALSE;
      }
   }

   VkFormatProperties props;
   vkGetPhysicalDeviceFormatProperties(screen->pdev, vkformat, &props);

   if (target == PIPE_BUFFER) {
      if (bind & PIPE_BIND_VERTEX_BUFFER &&
          !(props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
         return FALSE;
   } else {
      /* all other targets are texture-targets */
      if (bind & PIPE_BIND_RENDER_TARGET &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
         return FALSE;

      if (bind & PIPE_BIND_BLENDABLE &&
         !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT))
        return FALSE;

      if (bind & PIPE_BIND_SAMPLER_VIEW &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
         return FALSE;

      if (bind & PIPE_BIND_DEPTH_STENCIL &&
          !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
         return FALSE;
   }

   if (util_format_is_compressed(format)) {
      const struct util_format_description *desc = util_format_description(format);
      if (desc->layout == UTIL_FORMAT_LAYOUT_BPTC &&
          !screen->feats.textureCompressionBC)
         return FALSE;
   }

   return TRUE;
}

static void
zink_destroy_screen(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   slab_destroy_parent(&screen->transfer_pool);
   FREE(screen);
}

static VkInstance
create_instance()
{
   VkApplicationInfo ai = {};
   ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;

   char proc_name[128];
   if (os_get_process_name(proc_name, ARRAY_SIZE(proc_name)))
      ai.pApplicationName = proc_name;
   else
      ai.pApplicationName = "unknown";

   ai.pEngineName = "mesa zink";
   ai.apiVersion = VK_API_VERSION_1_0;

   const char *extensions[] = {
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
   };

   VkInstanceCreateInfo ici = {};
   ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   ici.pApplicationInfo = &ai;
   ici.ppEnabledExtensionNames = extensions;
   ici.enabledExtensionCount = ARRAY_SIZE(extensions);

   VkInstance instance = VK_NULL_HANDLE;
   VkResult err = vkCreateInstance(&ici, NULL, &instance);
   if (err != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return instance;
}

static VkPhysicalDevice
choose_pdev(const VkInstance instance)
{
   uint32_t i, pdev_count;
   VkPhysicalDevice *pdevs, pdev;
   vkEnumeratePhysicalDevices(instance, &pdev_count, NULL);
   assert(pdev_count > 0);

   pdevs = malloc(sizeof(*pdevs) * pdev_count);
   vkEnumeratePhysicalDevices(instance, &pdev_count, pdevs);
   assert(pdev_count > 0);

   pdev = pdevs[0];
   for (i = 0; i < pdev_count; ++i) {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(pdevs[i], &props);
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
         pdev = pdevs[i];
         break;
      }
   }
   free(pdevs);
   return pdev;
}

static uint32_t
find_gfx_queue(const VkPhysicalDevice pdev)
{
   uint32_t num_queues;
   vkGetPhysicalDeviceQueueFamilyProperties(pdev, &num_queues, NULL);
   assert(num_queues > 0);

   VkQueueFamilyProperties *props = malloc(sizeof(*props) * num_queues);
   vkGetPhysicalDeviceQueueFamilyProperties(pdev, &num_queues, props);

   for (uint32_t i = 0; i < num_queues; i++) {
      if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         free(props);
         return i;
      }
   }

   return UINT32_MAX;
}

static void
zink_flush_frontbuffer(struct pipe_screen *pscreen,
                       struct pipe_resource *pres,
                       unsigned level, unsigned layer,
                       void *winsys_drawable_handle,
                       struct pipe_box *sub_box)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct sw_winsys *winsys = screen->winsys;
   struct zink_resource *res = zink_resource(pres);

   if (!winsys)
     return;
   void *map = winsys->displaytarget_map(winsys, res->dt, 0);

   if (map) {
      VkImageSubresource isr = {};
      isr.aspectMask = res->aspect;
      isr.mipLevel = level;
      isr.arrayLayer = layer;
      VkSubresourceLayout layout;
      vkGetImageSubresourceLayout(screen->dev, res->image, &isr, &layout);

      void *ptr;
      VkResult result = vkMapMemory(screen->dev, res->mem, res->offset, res->size, 0, &ptr);
      if (result != VK_SUCCESS) {
         debug_printf("failed to map memory for display\n");
         return;
      }
      for (int i = 0; i < pres->height0; ++i) {
         uint8_t *src = (uint8_t *)ptr + i * layout.rowPitch;
         uint8_t *dst = (uint8_t *)map + i * res->dt_stride;
         memcpy(dst, src, res->dt_stride);
      }
      vkUnmapMemory(screen->dev, res->mem);
   }

   winsys->displaytarget_unmap(winsys, res->dt);

   assert(res->dt);
   if (res->dt)
      winsys->displaytarget_display(winsys, res->dt, winsys_drawable_handle, sub_box);
}

static struct pipe_screen *
zink_internal_create_screen(struct sw_winsys *winsys, int fd)
{
   struct zink_screen *screen = CALLOC_STRUCT(zink_screen);
   if (!screen)
      return NULL;

   zink_debug = debug_get_option_zink_debug();

   screen->instance = create_instance();
   screen->pdev = choose_pdev(screen->instance);
   screen->gfx_queue = find_gfx_queue(screen->pdev);

   vkGetPhysicalDeviceProperties(screen->pdev, &screen->props);
   vkGetPhysicalDeviceFeatures(screen->pdev, &screen->feats);
   vkGetPhysicalDeviceMemoryProperties(screen->pdev, &screen->mem_props);

   screen->have_X8_D24_UNORM_PACK32 = is_depth_format_supported(screen,
                                         VK_FORMAT_X8_D24_UNORM_PACK32);
   screen->have_D24_UNORM_S8_UINT = is_depth_format_supported(screen,
                                         VK_FORMAT_D24_UNORM_S8_UINT);

   uint32_t num_extensions = 0;
   if (vkEnumerateDeviceExtensionProperties(screen->pdev, NULL,
       &num_extensions, NULL) == VK_SUCCESS && num_extensions > 0) {
      VkExtensionProperties *extensions = MALLOC(sizeof(VkExtensionProperties) *
                                                num_extensions);
      if (extensions) {
         vkEnumerateDeviceExtensionProperties(screen->pdev, NULL,
                                              &num_extensions, extensions);

         for (uint32_t  i = 0; i < num_extensions; ++i) {
            if (!strcmp(extensions[i].extensionName,
                        VK_KHR_MAINTENANCE1_EXTENSION_NAME))
               screen->have_KHR_maintenance1 = true;
            if (!strcmp(extensions[i].extensionName,
                        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
               screen->have_KHR_external_memory_fd = true;
         }
         FREE(extensions);
      }
   }

   if (!screen->have_KHR_maintenance1) {
      debug_printf("ZINK: VK_KHR_maintenance1 required!\n");
      goto fail;
   }

   VkDeviceQueueCreateInfo qci = {};
   float dummy = 0.0f;
   qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   qci.queueFamilyIndex = screen->gfx_queue;
   qci.queueCount = 1;
   qci.pQueuePriorities = &dummy;

   VkDeviceCreateInfo dci = {};
   dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   dci.queueCreateInfoCount = 1;
   dci.pQueueCreateInfos = &qci;
   dci.pEnabledFeatures = &screen->feats;
   const char *extensions[3] = {
      VK_KHR_MAINTENANCE1_EXTENSION_NAME,
   };
   num_extensions = 1;

   if (fd >= 0 && !screen->have_KHR_external_memory_fd) {
      debug_printf("ZINK: KHR_external_memory_fd required!\n");
      goto fail;
   }

   if (screen->have_KHR_external_memory_fd) {
      extensions[num_extensions++] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
      extensions[num_extensions++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
   }
   assert(num_extensions <= ARRAY_SIZE(extensions));

   dci.ppEnabledExtensionNames = extensions;
   dci.enabledExtensionCount = num_extensions;
   if (vkCreateDevice(screen->pdev, &dci, NULL, &screen->dev) != VK_SUCCESS)
      goto fail;

   screen->winsys = winsys;

   screen->base.get_name = zink_get_name;
   screen->base.get_vendor = zink_get_vendor;
   screen->base.get_device_vendor = zink_get_device_vendor;
   screen->base.get_param = zink_get_param;
   screen->base.get_paramf = zink_get_paramf;
   screen->base.get_shader_param = zink_get_shader_param;
   screen->base.get_compiler_options = zink_get_compiler_options;
   screen->base.is_format_supported = zink_is_format_supported;
   screen->base.context_create = zink_context_create;
   screen->base.flush_frontbuffer = zink_flush_frontbuffer;
   screen->base.destroy = zink_destroy_screen;

   zink_screen_resource_init(&screen->base);
   zink_screen_fence_init(&screen->base);

   slab_create_parent(&screen->transfer_pool, sizeof(struct zink_transfer), 16);

   return &screen->base;

fail:
   FREE(screen);
   return NULL;
}

struct pipe_screen *
zink_create_screen(struct sw_winsys *winsys)
{
   return zink_internal_create_screen(winsys, -1);
}

struct pipe_screen *
zink_drm_create_screen(int fd)
{
   return zink_internal_create_screen(NULL, fd);
}
