/*
 * Copyright 2015 Intel Corporation
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice (including the next
 *  paragraph) shall be included in all copies or substantial portions of the
 *  Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#include <assert.h>

#include "isl.h"
#include "brw_device_info.h"

struct surface_format_info {
   bool exists;
   uint8_t sampling;
   uint8_t filtering;
   uint8_t shadow_compare;
   uint8_t chroma_key;
   uint8_t render_target;
   uint8_t alpha_blend;
   uint8_t input_vb;
   uint8_t streamed_output_vb;
   uint8_t color_processing;
   uint8_t lossless_compression;
};

/* This macro allows us to write the table almost as it appears in the PRM,
 * while restructuring it to turn it into the C code we want.
 */
#define SF(sampl, filt, shad, ck, rt, ab, vb, so, color, ccs_e, sf) \
   [ISL_FORMAT_##sf] = { true, sampl, filt, shad, ck, rt, ab, vb, so, color, ccs_e},

#define Y 0
#define x 255
/**
 * This is the table of support for surface (texture, renderbuffer, and vertex
 * buffer, but not depthbuffer) formats across the various hardware generations.
 *
 * The table is formatted to match the documentation, except that the docs have
 * this ridiculous mapping of Y[*+~^#&] for "supported on DevWhatever".  To put
 * it in our table, here's the mapping:
 *
 * Y*: 45
 * Y+: 45 (g45/gm45)
 * Y~: 50 (gen5)
 * Y^: 60 (gen6)
 * Y#: 70 (gen7)
 *
 * The abbreviations in the header below are:
 * smpl  - Sampling Engine
 * filt  - Sampling Engine Filtering
 * shad  - Sampling Engine Shadow Map
 * CK    - Sampling Engine Chroma Key
 * RT    - Render Target
 * AB    - Alpha Blend Render Target
 * VB    - Input Vertex Buffer
 * SO    - Steamed Output Vertex Buffers (transform feedback)
 * color - Color Processing
 * ccs_e - Lossless Compression Support (gen9+ only)
 * sf    - Surface Format
 *
 * See page 88 of the Sandybridge PRM VOL4_Part1 PDF.
 *
 * As of Ivybridge, the columns are no longer in that table and the
 * information can be found spread across:
 *
 * - VOL2_Part1 section 2.5.11 Format Conversion (vertex fetch).
 * - VOL4_Part1 section 2.12.2.1.2 Sampler Output Channel Mapping.
 * - VOL4_Part1 section 3.9.11 Render Target Write.
 * - Render Target Surface Types [SKL+]
 */
static const struct surface_format_info format_info[] = {
/* smpl filt shad CK  RT  AB  VB  SO  color ccs_e */
   SF( Y, 50,  x,  x,  Y,  Y,  Y,  Y,  x,   90,   R32G32B32A32_FLOAT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x,   90,   R32G32B32A32_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x,   90,   R32G32B32A32_UINT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32A32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32A32_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R64G64_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x,    x,   R32G32B32X32_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32A32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32A32_USCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R32G32B32A32_SFIXED)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   R64G64_PASSTHRU)
   SF( Y, 50,  x,  x,  x,  x,  Y,  Y,  x,    x,   R32G32B32_FLOAT)
   SF( Y,  x,  x,  x,  x,  x,  Y,  Y,  x,    x,   R32G32B32_SINT)
   SF( Y,  x,  x,  x,  x,  x,  Y,  Y,  x,    x,   R32G32B32_UINT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32B32_USCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R32G32B32_SFIXED)
   SF( Y,  Y,  x,  x,  Y, 45,  Y,  x, 60,   90,   R16G16B16A16_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x,   90,   R16G16B16A16_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,   90,   R16G16B16A16_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,   90,   R16G16B16A16_UINT)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x,   90,   R16G16B16A16_FLOAT)
   SF( Y, 50,  x,  x,  Y,  Y,  Y,  Y,  x,   90,   R32G32_FLOAT)
   SF( Y, 70,  x,  x,  Y,  Y,  Y,  Y,  x,    x,   R32G32_FLOAT_LD)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x,   90,   R32G32_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x,   90,   R32G32_UINT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   R32_FLOAT_X8X24_TYPELESS)
   SF( Y,  x,  x,  x,  x,  x,  x,  x,  x,    x,   X32_TYPELESS_G8X24_UINT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x,    x,   L32A32_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R64_FLOAT)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   R16G16B16X16_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,   90,   R16G16B16X16_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x,    x,   A32X32_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x,    x,   L32X32_FLOAT)
   SF( Y, 50,  x,  x,  x,  x,  x,  x,  x,    x,   I32X32_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16B16A16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16B16A16_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32G32_USCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R32G32_SFIXED)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   R64_PASSTHRU)
   SF( Y,  Y,  x,  Y,  Y,  Y,  Y,  x, 60,   90,   B8G8R8A8_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x,    x,   B8G8R8A8_UNORM_SRGB)
/* smpl filt shad CK  RT  AB  VB  SO  color ccs_e */
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x, 60,    x,   R10G10B10A2_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x, 60,    x,   R10G10B10A2_UNORM_SRGB)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R10G10B10A2_UINT)
   SF( Y,  Y,  x,  x,  x,  x,  Y,  x,  x,    x,   R10G10B10_SNORM_A2_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x, 60,   90,   R8G8B8A8_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x, 60,    x,   R8G8B8A8_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x,   90,   R8G8B8A8_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,   90,   R8G8B8A8_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,   90,   R8G8B8A8_UINT)
   SF( Y,  Y,  x,  x,  Y, 45,  Y,  x,  x,   90,   R16G16_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x,   90,   R16G16_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,   90,   R16G16_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,   90,   R16G16_UINT)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x,   90,   R16G16_FLOAT)
   SF( Y,  Y,  x,  x,  Y,  Y, 75,  x, 60,    x,   B10G10R10A2_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x, 60,    x,   B10G10R10A2_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x,    x,   R11G11B10_FLOAT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x,   90,   R32_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  Y,  x,   90,   R32_UINT)
   SF( Y, 50,  Y,  x,  Y,  Y,  Y,  Y,  x,   90,   R32_FLOAT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   R24_UNORM_X8_TYPELESS)
   SF( Y,  x,  x,  x,  x,  x,  x,  x,  x,    x,   X24_TYPELESS_G8_UINT)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   L16A16_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   I24X8_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   L24X8_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   A24X8_UNORM)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   I32_FLOAT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   L32_FLOAT)
   SF( Y, 50,  Y,  x,  x,  x,  x,  x,  x,    x,   A32_FLOAT)
   SF( Y,  Y,  x,  Y, 80, 80,  x,  x, 60,   90,   B8G8R8X8_UNORM)
   SF( Y,  Y,  x,  x, 80, 80,  x,  x,  x,    x,   B8G8R8X8_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   R8G8B8X8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   R8G8B8X8_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   R9G9B9E5_SHAREDEXP)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   B10G10R10X2_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   L16A16_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32_SNORM)
/* smpl filt shad CK  RT  AB  VB  SO  color ccs_e */
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R10G10B10X2_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8B8A8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8B8A8_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R32_USCALED)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x,    x,   B5G6R5_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x,    x,   B5G6R5_UNORM_SRGB)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x,    x,   B5G5R5A1_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x,    x,   B5G5R5A1_UNORM_SRGB)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x,    x,   B4G4R4A4_UNORM)
   SF( Y,  Y,  x,  x,  Y,  Y,  x,  x,  x,    x,   B4G4R4A4_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x,    x,   R8G8_UNORM)
   SF( Y,  Y,  x,  Y,  Y, 60,  Y,  x,  x,    x,   R8G8_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R8G8_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R8G8_UINT)
   SF( Y,  Y,  Y,  x,  Y, 45,  Y,  x, 70,    x,   R16_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x,    x,   R16_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R16_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R16_UINT)
   SF( Y,  Y,  x,  x,  Y,  Y,  Y,  x,  x,    x,   R16_FLOAT)
   SF(50, 50,  x,  x,  x,  x,  x,  x,  x,    x,   A8P8_UNORM_PALETTE0)
   SF(50, 50,  x,  x,  x,  x,  x,  x,  x,    x,   A8P8_UNORM_PALETTE1)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x,    x,   I16_UNORM)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x,    x,   L16_UNORM)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x,    x,   A16_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x,    x,   L8A8_UNORM)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x,    x,   I16_FLOAT)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x,    x,   L16_FLOAT)
   SF( Y,  Y,  Y,  x,  x,  x,  x,  x,  x,    x,   A16_FLOAT)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   L8A8_UNORM_SRGB)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x,    x,   R5G5_SNORM_B6_UNORM)
   SF( x,  x,  x,  x,  Y,  Y,  x,  x,  x,    x,   B5G5R5X1_UNORM)
   SF( x,  x,  x,  x,  Y,  Y,  x,  x,  x,    x,   B5G5R5X1_UNORM_SRGB)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8_USCALED)
/* smpl filt shad CK  RT  AB  VB  SO  color ccs_e */
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16_USCALED)
   SF(50, 50,  x,  x,  x,  x,  x,  x,  x,    x,   P8A8_UNORM_PALETTE0)
   SF(50, 50,  x,  x,  x,  x,  x,  x,  x,    x,   P8A8_UNORM_PALETTE1)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   A1B5G5R5_UNORM)
   SF(90, 90,  x,  x, 90,  x,  x,  x,  x,    x,   A4B4G4R4_UNORM)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   L8A8_UINT)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   L8A8_SINT)
   SF( Y,  Y,  x, 45,  Y,  Y,  Y,  x,  x,    x,   R8_UNORM)
   SF( Y,  Y,  x,  x,  Y, 60,  Y,  x,  x,    x,   R8_SNORM)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R8_SINT)
   SF( Y,  x,  x,  x,  Y,  x,  Y,  x,  x,    x,   R8_UINT)
   SF( Y,  Y,  x,  Y,  Y,  Y,  x,  x,  x,    x,   A8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   I8_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x,    x,   L8_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   P4A4_UNORM_PALETTE0)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   A4P4_UNORM_PALETTE0)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8_USCALED)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   P8_UNORM_PALETTE0)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   L8_UNORM_SRGB)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   P8_UNORM_PALETTE1)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   P4A4_UNORM_PALETTE1)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   A4P4_UNORM_PALETTE1)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   Y8_UNORM)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   L8_UINT)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   L8_SINT)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   I8_UINT)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   I8_SINT)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   DXT1_RGB_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   R1_UNORM)
   SF( Y,  Y,  x,  Y,  Y,  x,  x,  x, 60,    x,   YCRCB_NORMAL)
   SF( Y,  Y,  x,  Y,  Y,  x,  x,  x, 60,    x,   YCRCB_SWAPUVY)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   P2_UNORM_PALETTE0)
   SF(45, 45,  x,  x,  x,  x,  x,  x,  x,    x,   P2_UNORM_PALETTE1)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x,    x,   BC1_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x,    x,   BC2_UNORM)
   SF( Y,  Y,  x,  Y,  x,  x,  x,  x,  x,    x,   BC3_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC4_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC5_UNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC1_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC2_UNORM_SRGB)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC3_UNORM_SRGB)
   SF( Y,  x,  x,  x,  x,  x,  x,  x,  x,    x,   MONO8)
   SF( Y,  Y,  x,  x,  Y,  x,  x,  x, 60,    x,   YCRCB_SWAPUV)
   SF( Y,  Y,  x,  x,  Y,  x,  x,  x, 60,    x,   YCRCB_SWAPY)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   DXT1_RGB)
/* smpl filt shad CK  RT  AB  VB  SO  color ccs_e */
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   FXT1)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8B8_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8B8_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8B8_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R8G8B8_USCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R64G64B64A64_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R64G64B64_FLOAT)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC4_SNORM)
   SF( Y,  Y,  x,  x,  x,  x,  x,  x,  x,    x,   BC5_SNORM)
   SF(50, 50,  x,  x,  x,  x, 60,  x,  x,    x,   R16G16B16_FLOAT)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16B16_UNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16B16_SNORM)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16B16_SSCALED)
   SF( x,  x,  x,  x,  x,  x,  Y,  x,  x,    x,   R16G16B16_USCALED)
   SF(70, 70,  x,  x,  x,  x,  x,  x,  x,    x,   BC6H_SF16)
   SF(70, 70,  x,  x,  x,  x,  x,  x,  x,    x,   BC7_UNORM)
   SF(70, 70,  x,  x,  x,  x,  x,  x,  x,    x,   BC7_UNORM_SRGB)
   SF(70, 70,  x,  x,  x,  x,  x,  x,  x,    x,   BC6H_UF16)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   PLANAR_420_8)
   SF( x,  x,  x,  x,  x,  x,  x,  x,  x,    x,   R8G8B8_UNORM_SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC1_RGB8)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC2_RGB8)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   EAC_R11)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   EAC_RG11)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   EAC_SIGNED_R11)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   EAC_SIGNED_RG11)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC2_SRGB8)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R16G16B16_UINT)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R16G16B16_SINT)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R32_SFIXED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R10G10B10A2_SNORM)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R10G10B10A2_USCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R10G10B10A2_SSCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R10G10B10A2_SINT)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   B10G10R10A2_SNORM)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   B10G10R10A2_USCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   B10G10R10A2_SSCALED)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   B10G10R10A2_UINT)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   B10G10R10A2_SINT)
   SF( x,  x,  x,  x,  x,  x, 80,  x,  x,    x,   R64G64B64A64_PASSTHRU)
   SF( x,  x,  x,  x,  x,  x, 80,  x,  x,    x,   R64G64B64_PASSTHRU)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC2_RGB8_PTA)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC2_SRGB8_PTA)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC2_EAC_RGBA8)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ETC2_EAC_SRGB8_A8)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R8G8B8_UINT)
   SF( x,  x,  x,  x,  x,  x, 75,  x,  x,    x,   R8G8B8_SINT)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_4X4_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_5X4_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_5X5_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_6X5_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_6X6_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_8X5_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_8X6_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_8X8_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X5_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X6_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X8_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X10_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_12X10_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_12X12_FLT16)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_4X4_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_5X4_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_5X5_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_6X5_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_6X6_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_8X5_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_8X6_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_8X8_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X5_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X6_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X8_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_10X10_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_12X10_U8SRGB)
   SF(80, 80,  x,  x,  x,  x,  x,  x,  x,    x,   ASTC_LDR_2D_12X12_U8SRGB)
};
#undef x
#undef Y

static unsigned
format_gen(const struct brw_device_info *devinfo)
{
   return devinfo->gen * 10 + (devinfo->is_g4x || devinfo->is_haswell) * 5;
}

bool
isl_format_supports_rendering(const struct brw_device_info *devinfo,
                              enum isl_format format)
{
   if (!format_info[format].exists)
      return false;

   return format_gen(devinfo) >= format_info[format].render_target;
}

bool
isl_format_supports_alpha_blending(const struct brw_device_info *devinfo,
                                   enum isl_format format)
{
   if (!format_info[format].exists)
      return false;

   return format_gen(devinfo) >= format_info[format].alpha_blend;
}

bool
isl_format_supports_sampling(const struct brw_device_info *devinfo,
                             enum isl_format format)
{
   if (!format_info[format].exists)
      return false;

   return format_gen(devinfo) >= format_info[format].sampling;
}

bool
isl_format_supports_filtering(const struct brw_device_info *devinfo,
                              enum isl_format format)
{
   if (!format_info[format].exists)
      return false;

   return format_gen(devinfo) >= format_info[format].filtering;
}

bool
isl_format_supports_vertex_fetch(const struct brw_device_info *devinfo,
                                 enum isl_format format)
{
   if (!format_info[format].exists)
      return false;

   /* For vertex fetch, Bay Trail supports the same set of formats as Haswell
    * but is a superset of Ivy Bridge.
    */
   if (devinfo->is_baytrail)
      return 75 >= format_info[format].input_vb;

   return format_gen(devinfo) >= format_info[format].input_vb;
}

bool
isl_format_supports_lossless_compression(const struct brw_device_info *devinfo,
                                         enum isl_format format)
{
   if (!format_info[format].exists)
      return false;

   return format_gen(devinfo) >= format_info[format].lossless_compression;
}

static inline bool
isl_format_has_channel_type(enum isl_format fmt, enum isl_base_type type)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(fmt);

   return fmtl->channels.r.type == type ||
          fmtl->channels.g.type == type ||
          fmtl->channels.b.type == type ||
          fmtl->channels.a.type == type ||
          fmtl->channels.l.type == type ||
          fmtl->channels.i.type == type ||
          fmtl->channels.p.type == type;
}

bool
isl_format_has_unorm_channel(enum isl_format fmt)
{
   return isl_format_has_channel_type(fmt, ISL_UNORM);
}

bool
isl_format_has_snorm_channel(enum isl_format fmt)
{
   return isl_format_has_channel_type(fmt, ISL_SNORM);
}

bool
isl_format_has_ufloat_channel(enum isl_format fmt)
{
   return isl_format_has_channel_type(fmt, ISL_UFLOAT);
}

bool
isl_format_has_sfloat_channel(enum isl_format fmt)
{
   return isl_format_has_channel_type(fmt, ISL_SFLOAT);
}

bool
isl_format_has_uint_channel(enum isl_format fmt)
{
   return isl_format_has_channel_type(fmt, ISL_UINT);
}

bool
isl_format_has_sint_channel(enum isl_format fmt)
{
   return isl_format_has_channel_type(fmt, ISL_SINT);
}

unsigned
isl_format_get_num_channels(enum isl_format fmt)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(fmt);

   assert(fmtl->channels.p.bits == 0);

   return (fmtl->channels.r.bits > 0) +
          (fmtl->channels.g.bits > 0) +
          (fmtl->channels.b.bits > 0) +
          (fmtl->channels.a.bits > 0) +
          (fmtl->channels.l.bits > 0) +
          (fmtl->channels.i.bits > 0);
}

enum isl_format
isl_format_rgb_to_rgba(enum isl_format rgb)
{
   assert(isl_format_is_rgb(rgb));

   switch (rgb) {
   case ISL_FORMAT_R32G32B32_FLOAT:    return ISL_FORMAT_R32G32B32A32_FLOAT;
   case ISL_FORMAT_R32G32B32_SINT:     return ISL_FORMAT_R32G32B32A32_SINT;
   case ISL_FORMAT_R32G32B32_UINT:     return ISL_FORMAT_R32G32B32A32_UINT;
   case ISL_FORMAT_R32G32B32_UNORM:    return ISL_FORMAT_R32G32B32A32_UNORM;
   case ISL_FORMAT_R32G32B32_SNORM:    return ISL_FORMAT_R32G32B32A32_SNORM;
   case ISL_FORMAT_R32G32B32_SSCALED:  return ISL_FORMAT_R32G32B32A32_SSCALED;
   case ISL_FORMAT_R32G32B32_USCALED:  return ISL_FORMAT_R32G32B32A32_USCALED;
   case ISL_FORMAT_R32G32B32_SFIXED:   return ISL_FORMAT_R32G32B32A32_SFIXED;
   case ISL_FORMAT_R8G8B8_UNORM:       return ISL_FORMAT_R8G8B8A8_UNORM;
   case ISL_FORMAT_R8G8B8_SNORM:       return ISL_FORMAT_R8G8B8A8_SNORM;
   case ISL_FORMAT_R8G8B8_SSCALED:     return ISL_FORMAT_R8G8B8A8_SSCALED;
   case ISL_FORMAT_R8G8B8_USCALED:     return ISL_FORMAT_R8G8B8A8_USCALED;
   case ISL_FORMAT_R16G16B16_FLOAT:    return ISL_FORMAT_R16G16B16A16_FLOAT;
   case ISL_FORMAT_R16G16B16_UNORM:    return ISL_FORMAT_R16G16B16A16_UNORM;
   case ISL_FORMAT_R16G16B16_SNORM:    return ISL_FORMAT_R16G16B16A16_SNORM;
   case ISL_FORMAT_R16G16B16_SSCALED:  return ISL_FORMAT_R16G16B16A16_SSCALED;
   case ISL_FORMAT_R16G16B16_USCALED:  return ISL_FORMAT_R16G16B16A16_USCALED;
   case ISL_FORMAT_R8G8B8_UNORM_SRGB:  return ISL_FORMAT_R8G8B8A8_UNORM_SRGB;
   case ISL_FORMAT_R16G16B16_UINT:     return ISL_FORMAT_R16G16B16A16_UINT;
   case ISL_FORMAT_R16G16B16_SINT:     return ISL_FORMAT_R16G16B16A16_SINT;
   case ISL_FORMAT_R8G8B8_UINT:        return ISL_FORMAT_R8G8B8A8_UINT;
   case ISL_FORMAT_R8G8B8_SINT:        return ISL_FORMAT_R8G8B8A8_SINT;
   default:
      return ISL_FORMAT_UNSUPPORTED;
   }
}

enum isl_format
isl_format_rgb_to_rgbx(enum isl_format rgb)
{
   assert(isl_format_is_rgb(rgb));

   switch (rgb) {
   case ISL_FORMAT_R32G32B32_FLOAT:
      return ISL_FORMAT_R32G32B32X32_FLOAT;
   case ISL_FORMAT_R16G16B16_UNORM:
      return ISL_FORMAT_R16G16B16X16_UNORM;
   case ISL_FORMAT_R16G16B16_FLOAT:
      return ISL_FORMAT_R16G16B16X16_FLOAT;
   case ISL_FORMAT_R8G8B8_UNORM:
      return ISL_FORMAT_R8G8B8X8_UNORM;
   case ISL_FORMAT_R8G8B8_UNORM_SRGB:
      return ISL_FORMAT_R8G8B8X8_UNORM_SRGB;
   default:
      return ISL_FORMAT_UNSUPPORTED;
   }
}
