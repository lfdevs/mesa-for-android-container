/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_format.h"

#include "kk_buffer_view.h"
#include "kk_entrypoints.h"
#include "kk_image.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_format.h"

#include "vk_enum_defines.h"
#include "vk_format.h"

#define MTL_FMT_ALL_NO_ATOMIC(width)                                           \
   .bit_widths = width, .filter = 1u, .write = 1u, .color = 1u, .blend = 1u,   \
   .msaa = 1u, .resolve = 1u, .sparse = 1u, .atomic = 0u

// Filter, Write, Color, Blend, MSAA, Sparse
#define MTL_FMT_FWCBMS(width)                                                  \
   .bit_widths = width, .filter = 1u, .write = 1u, .color = 1u, .blend = 1u,   \
   .msaa = 1u, .resolve = 0u, .sparse = 1u, .atomic = 0u

// Filter, Color, Blend, MSAA, Resolve, Sparse
#define MTL_FMT_FCBMRS(width)                                                  \
   .bit_widths = width, .filter = 1u, .write = 0u, .color = 1u, .blend = 1u,   \
   .msaa = 1u, .resolve = 1u, .sparse = 1u, .atomic = 0u

// Filter, Write, Color, Blend, MSAA
#define MTL_FMT_FWCBM(width)                                                   \
   .bit_widths = width, .filter = 1u, .write = 1u, .color = 1u, .blend = 1u,   \
   .msaa = 1u, .resolve = 0u, .sparse = 0u, .atomic = 0u

// Write, Color, Blend, MSAA, Sparse
#define MTL_FMT_WCBMS(width)                                                   \
   .bit_widths = width, .filter = 0u, .write = 1u, .color = 1u, .blend = 1u,   \
   .msaa = 1u, .resolve = 0u, .sparse = 0u, .atomic = 0u

// Write, Color, MSAA, Sparse
#define MTL_FMT_WCMS(width)                                                    \
   .bit_widths = width, .filter = 0u, .write = 1u, .color = 1u, .blend = 0u,   \
   .msaa = 1u, .resolve = 0u, .sparse = 1u, .atomic = 0u

// Write, Color, Sparse, Atomic
#define MTL_FMT_WCSA(width)                                                    \
   .bit_widths = width, .filter = 0u, .write = 1u, .color = 1u, .blend = 0u,   \
   .msaa = 0u, .resolve = 0u, .sparse = 1u, .atomic = 1u

// Write, Color, Sparse
#define MTL_FMT_WCS(width)                                                     \
   .bit_widths = width, .filter = 0u, .write = 1u, .color = 1u, .blend = 0u,   \
   .msaa = 0u, .resolve = 0u, .sparse = 1u, .atomic = 0u

// Filter, MSAA, Resolve
#define MTL_FMT_FMR(width)                                                     \
   .bit_widths = width, .filter = 1u, .write = 0u, .color = 0u, .blend = 0u,   \
   .msaa = 1u, .resolve = 1u, .sparse = 0u, .atomic = 0u

// Filter, Sparse
#define MTL_FMT_FS(width)                                                      \
   .bit_widths = width, .filter = 1u, .write = 0u, .color = 0u, .blend = 0u,   \
   .msaa = 0u, .resolve = 0u, .sparse = 1u, .atomic = 0u

// MSAA, Resolve
#define MTL_FMT_MR(width)                                                      \
   .bit_widths = width, .filter = 0u, .write = 0u, .color = 0u, .blend = 0u,   \
   .msaa = 1u, .resolve = 1u, .sparse = 0u, .atomic = 0u

// MSAA
#define MTL_FMT_M(width)                                                       \
   .bit_widths = width, .filter = 0u, .write = 0u, .color = 0u, .blend = 0u,   \
   .msaa = 1u, .resolve = 0u, .sparse = 0u, .atomic = 0u

#define MTL_FMT_TB_ALL                                                         \
   .texel_buffer = {                                                           \
      .write = 1u,                                                             \
      .read = 1u,                                                              \
      .read_write = 1u,                                                        \
   }

#define MTL_FMT_TB_WR                                                          \
   .texel_buffer = {                                                           \
      .write = 1u,                                                             \
      .read = 1u,                                                              \
      .read_write = 0u,                                                        \
   }

#define MTL_FMT_TB_R                                                           \
   .texel_buffer = {                                                           \
      .write = 0u,                                                             \
      .read = 1u,                                                              \
      .read_write = 0u,                                                        \
   }

#define MTL_FMT_TB_NONE                                                        \
   .texel_buffer = {                                                           \
      .write = 0u,                                                             \
      .read = 0u,                                                              \
      .read_write = 0u,                                                        \
   }

#define MTL_SWIZZLE_IDENTITY                                                   \
   .unswizzle = {                                                              \
      .red = PIPE_SWIZZLE_X,                                                   \
      .green = PIPE_SWIZZLE_Y,                                                 \
      .blue = PIPE_SWIZZLE_Z,                                                  \
      .alpha = PIPE_SWIZZLE_W,                                                 \
   }

#define MTL_SWIZZLE_ABGR                                                       \
   .unswizzle = {                                                              \
      .red = PIPE_SWIZZLE_W,                                                   \
      .green = PIPE_SWIZZLE_Z,                                                 \
      .blue = PIPE_SWIZZLE_Y,                                                  \
      .alpha = PIPE_SWIZZLE_X,                                                 \
   }

#define MTL_SWIZZLE_GBAR                                                       \
   .unswizzle = {                                                              \
      .red = PIPE_SWIZZLE_Y,                                                   \
      .green = PIPE_SWIZZLE_Z,                                                 \
      .blue = PIPE_SWIZZLE_W,                                                  \
      .alpha = PIPE_SWIZZLE_X,                                                 \
   }

#define MTL_SWIZZLE_BGRA                                                       \
   .unswizzle = {                                                              \
      .red = PIPE_SWIZZLE_Z,                                                   \
      .green = PIPE_SWIZZLE_Y,                                                 \
      .blue = PIPE_SWIZZLE_X,                                                  \
      .alpha = PIPE_SWIZZLE_W,                                                 \
   }

#define MTL_SWIZZLE_RGB1                                                       \
   .unswizzle = {                                                              \
      .red = PIPE_SWIZZLE_X,                                                   \
      .green = PIPE_SWIZZLE_Y,                                                 \
      .blue = PIPE_SWIZZLE_Z,                                                  \
      .alpha = PIPE_SWIZZLE_1,                                                 \
   }

#define MTL_FMT(pipe_format, mtl_format, swizzle, capabilities,                \
                texel_buffer_capabilities, native)                             \
   [PIPE_FORMAT_##                                                             \
      pipe_format] = {.mtl_pixel_format = MTL_PIXEL_FORMAT_##mtl_format,       \
                      swizzle,                                                 \
                      capabilities,                                            \
                      texel_buffer_capabilities,                               \
                      .is_native = native}

#define MTL_FMT_NATIVE(format, capabilities, texel_buffer_capabilities)        \
   [PIPE_FORMAT_##format] = {.mtl_pixel_format = MTL_PIXEL_FORMAT_##format,    \
                             MTL_SWIZZLE_IDENTITY,                             \
                             capabilities,                                     \
                             texel_buffer_capabilities,                        \
                             .is_native = 1}

static const struct kk_va_format kk_vf_formats[] = {
   // 8-bit formats
   MTL_FMT_NATIVE(R8_UNORM, MTL_FMT_ALL_NO_ATOMIC(8), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(A8_UNORM, MTL_FMT_ALL_NO_ATOMIC(8), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R8_SRGB, MTL_FMT_ALL_NO_ATOMIC(8), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(R8_SNORM, MTL_FMT_ALL_NO_ATOMIC(8), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8_UINT, MTL_FMT_WCMS(8), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R8_SINT, MTL_FMT_WCMS(8), MTL_FMT_TB_ALL),

   // 16-bit formats
   MTL_FMT_NATIVE(R16_UNORM, MTL_FMT_FWCBMS(16), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16_SNORM, MTL_FMT_FWCBMS(16), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16_UINT, MTL_FMT_WCMS(16), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R16_SINT, MTL_FMT_WCMS(16), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R16_FLOAT, MTL_FMT_ALL_NO_ATOMIC(16), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R8G8_UNORM, MTL_FMT_ALL_NO_ATOMIC(16), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8G8_SNORM, MTL_FMT_ALL_NO_ATOMIC(16), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8G8_SRGB, MTL_FMT_ALL_NO_ATOMIC(16), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(R8G8_UINT, MTL_FMT_WCMS(16), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8G8_SINT, MTL_FMT_WCMS(16), MTL_FMT_TB_WR),

   // 32-bit formats
   MTL_FMT_NATIVE(R32_UINT, MTL_FMT_WCSA(32), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R32_SINT, MTL_FMT_WCSA(32), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R32_FLOAT, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R16G16_UNORM, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16_SNORM, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16_UINT, MTL_FMT_WCMS(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16_SINT, MTL_FMT_WCMS(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16_FLOAT, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8G8B8A8_UNORM, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8G8B8A8_SNORM, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R8G8B8A8_SRGB, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(R8G8B8A8_UINT, MTL_FMT_WCMS(32), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R8G8B8A8_SINT, MTL_FMT_WCMS(32), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(B8G8R8A8_UNORM, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_R),
   MTL_FMT_NATIVE(B8G8R8A8_SRGB, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_NONE),

   // 64-bit formats
   MTL_FMT_NATIVE(R32G32_UINT, MTL_FMT_WCMS(64), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R32G32_SINT, MTL_FMT_WCMS(64), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R32G32_FLOAT, MTL_FMT_ALL_NO_ATOMIC(64), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16B16A16_UNORM, MTL_FMT_ALL_NO_ATOMIC(64), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16B16A16_SNORM, MTL_FMT_ALL_NO_ATOMIC(64), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R16G16B16A16_UINT, MTL_FMT_WCMS(64), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R16G16B16A16_SINT, MTL_FMT_WCMS(64), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R16G16B16A16_FLOAT, MTL_FMT_ALL_NO_ATOMIC(64),
                  MTL_FMT_TB_ALL),

   // 128-bit formats
   MTL_FMT_NATIVE(R32G32B32A32_UINT, MTL_FMT_WCS(128), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R32G32B32A32_SINT, MTL_FMT_WCS(128), MTL_FMT_TB_ALL),
   MTL_FMT_NATIVE(R32G32B32A32_FLOAT, MTL_FMT_ALL_NO_ATOMIC(128),
                  MTL_FMT_TB_ALL),

   // 16-bit packed formats
   MTL_FMT_NATIVE(B5G6R5_UNORM, MTL_FMT_FCBMRS(16), MTL_FMT_TB_NONE),
   /* Hardware has issues with border color opaque black, and since it's not
    * required by Vulkan, we can just disable it.
    */
   /* MTL_FMT_NATIVE(A1B5G5R5_UNORM, MTL_FMT_FCBMRS(16), MTL_FMT_TB_NONE), */
   MTL_FMT_NATIVE(B5G5R5A1_UNORM, MTL_FMT_FCBMRS(16), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(A4B4G4R4_UNORM, MTL_FMT_FCBMRS(16), MTL_FMT_TB_NONE),
   MTL_FMT(R4G4B4A4_UNORM, A4B4G4R4_UNORM, MTL_SWIZZLE_ABGR, MTL_FMT_FCBMRS(16),
           MTL_FMT_TB_NONE, false),
   MTL_FMT(A4R4G4B4_UNORM, A4B4G4R4_UNORM, MTL_SWIZZLE_BGRA, MTL_FMT_FCBMRS(16),
           MTL_FMT_TB_NONE, false),
   MTL_FMT(B4G4R4A4_UNORM, A4B4G4R4_UNORM, MTL_SWIZZLE_GBAR, MTL_FMT_FCBMRS(16),
           MTL_FMT_TB_NONE, false),

   // 32-bit packed formats
   MTL_FMT_NATIVE(R10G10B10A2_UNORM, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(B10G10R10A2_UNORM, MTL_FMT_ALL_NO_ATOMIC(32),
                  MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(R10G10B10A2_UINT, MTL_FMT_WCMS(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R11G11B10_FLOAT, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_WR),
   MTL_FMT_NATIVE(R9G9B9E5_FLOAT, MTL_FMT_ALL_NO_ATOMIC(32), MTL_FMT_TB_NONE),

   // ASTC formats
   MTL_FMT_NATIVE(ASTC_4x4, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_5x4, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_5x5, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_6x5, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_6x6, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x5, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x6, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x8, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x5, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x6, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x8, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x10, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_12x10, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_12x12, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   MTL_FMT_NATIVE(ASTC_4x4_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_5x4_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_5x5_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_6x5_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_6x6_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x5_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x6_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x8_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x5_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x6_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x8_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x10_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_12x10_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_12x12_SRGB, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   MTL_FMT_NATIVE(ASTC_4x4_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_5x4_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_5x5_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_6x5_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_6x6_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x5_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x6_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_8x8_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x5_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x6_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x8_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_10x10_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_12x10_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ASTC_12x12_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   // EAC/ETC formats
   MTL_FMT_NATIVE(ETC2_R11_UNORM, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_R11_SNORM, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_RG11_UNORM, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_RG11_SNORM, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_RGBA8, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_SRGBA8, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   MTL_FMT_NATIVE(ETC2_RGB8, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_SRGB8, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_RGB8A1, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(ETC2_SRGB8A1, MTL_FMT_FS(64), MTL_FMT_TB_NONE),

   // BC/DXT formats
   MTL_FMT(DXT1_RGB, DXT1_RGBA, MTL_SWIZZLE_RGB1, MTL_FMT_FS(64),
           MTL_FMT_TB_NONE, true),
   MTL_FMT(DXT1_SRGB, DXT1_SRGBA, MTL_SWIZZLE_RGB1, MTL_FMT_FS(64),
           MTL_FMT_TB_NONE, true),
   MTL_FMT_NATIVE(DXT1_RGBA, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(DXT1_SRGBA, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(DXT3_RGBA, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(DXT3_SRGBA, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(DXT5_RGBA, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(DXT5_SRGBA, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   // RGTC formats
   MTL_FMT_NATIVE(RGTC1_UNORM, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(RGTC1_SNORM, MTL_FMT_FS(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(RGTC2_UNORM, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(RGTC2_SNORM, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   // BPTC formats
   MTL_FMT_NATIVE(BPTC_RGB_FLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(BPTC_RGB_UFLOAT, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(BPTC_RGBA_UNORM, MTL_FMT_FS(128), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(BPTC_SRGBA, MTL_FMT_FS(128), MTL_FMT_TB_NONE),

   // Compressed PVRTC, HDR ASTC, TODO_KOSMICKRISP
   // YUV formats TODO_KOSMICKRISP
   // Extended range and wide color formats TODO_KOSMICKRISP

   // Depth and stencil formats
   MTL_FMT_NATIVE(Z16_UNORM, MTL_FMT_FMR(16), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(Z32_FLOAT, MTL_FMT_FMR(32), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(S8_UINT, MTL_FMT_M(8), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(Z32_FLOAT_S8X24_UINT, MTL_FMT_FMR(64), MTL_FMT_TB_NONE),
   MTL_FMT_NATIVE(X32_S8X24_UINT, MTL_FMT_MR(64), MTL_FMT_TB_NONE),
};

#undef MTL_FMT_NATIVE
#undef MTL_FMT

#undef MTL_SWIZZLE_BGRA
#undef MTL_SWIZZLE_ABGR
#undef MTL_SWIZZLE_IDENTITY

#undef MTL_FMT_ALL_NO_ATOMIC
#undef MTL_FMT_FWCBMS
#undef MTL_FMT_FCBMRS
#undef MTL_FMT_FWCBM
#undef MTL_FMT_WCBMS
#undef MTL_FMT_WCMS
#undef MTL_FMT_WCSA
#undef MTL_FMT_WCS
#undef MTL_FMT_FMR
#undef MTL_FMT_FS
#undef MTL_FMT_MR
#undef MTL_FMT_M

#undef MTL_FMT_TB_ALL
#undef MTL_FMT_TB_WR
#undef MTL_FMT_TB_R
#undef MTL_FMT_TB_NONE

const struct kk_va_format *
kk_get_va_format(enum pipe_format format)
{
   if (format >= ARRAY_SIZE(kk_vf_formats))
      return NULL;

   if (kk_vf_formats[format].bit_widths == 0)
      return NULL;

   return &kk_vf_formats[format];
}

enum mtl_pixel_format
vk_format_to_mtl_pixel_format(VkFormat vkformat)
{
   enum pipe_format format = vk_format_to_pipe_format(vkformat);
   const struct kk_va_format *supported_format = kk_get_va_format(format);
   assert(supported_format);
   return supported_format->mtl_pixel_format;
}

VKAPI_ATTR void VKAPI_CALL
kk_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                      VkFormat format,
                                      VkFormatProperties2 *pFormatProperties)
{
   VK_FROM_HANDLE(kk_physical_device, pdevice, physicalDevice);

   VkFormatFeatureFlags2 linear2, optimal2, buffer2;
   linear2 =
      kk_get_image_format_features(pdevice, format, VK_IMAGE_TILING_LINEAR, 0);
   optimal2 =
      kk_get_image_format_features(pdevice, format, VK_IMAGE_TILING_OPTIMAL, 0);
   buffer2 = kk_get_buffer_format_features(pdevice, format);

   pFormatProperties->formatProperties = (VkFormatProperties){
      .linearTilingFeatures = vk_format_features2_to_features(linear2),
      .optimalTilingFeatures = vk_format_features2_to_features(optimal2),
      .bufferFeatures = vk_format_features2_to_features(buffer2),
   };

   vk_foreach_struct(ext, pFormatProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3: {
         VkFormatProperties3 *p = (void *)ext;
         p->linearTilingFeatures = linear2;
         p->optimalTilingFeatures = optimal2;
         p->bufferFeatures = buffer2;
         break;
      }

      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}
