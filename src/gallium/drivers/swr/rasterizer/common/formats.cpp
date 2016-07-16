
/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
* 
* @file formats.cpp
* 
* @brief auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/

#include "formats.h"

// lookup table for unorm8 srgb -> float conversion
const uint32_t srgb8Table[256] = {
    0x00000000, 0x399f22b4, 0x3a1f22b4, 0x3a6eb40f, 0x3a9f22b4, 0x3ac6eb61, 0x3aeeb40f, 0x3b0b3e5e, 0x3b1f22b4, 0x3b33070b, 0x3b46eb61, 0x3b5b518d, 0x3b70f18d, 0x3b83e1c6, 0x3b8fe616, 0x3b9c87fd,
    0x3ba9c9b5, 0x3bb7ad6f, 0x3bc63549, 0x3bd5635f, 0x3be539c1, 0x3bf5ba70, 0x3c0373b5, 0x3c0c6152, 0x3c15a703, 0x3c1f45be, 0x3c293e6b, 0x3c3391f7, 0x3c3e4149, 0x3c494d43, 0x3c54b6c7, 0x3c607eb1,
    0x3c6ca5dc, 0x3c792d22, 0x3c830aa8, 0x3c89af9f, 0x3c9085db, 0x3c978dc5, 0x3c9ec7c0, 0x3ca63431, 0x3cadd37d, 0x3cb5a601, 0x3cbdac20, 0x3cc5e639, 0x3cce54ab, 0x3cd6f7d3, 0x3cdfd00e, 0x3ce8ddb9,
    0x3cf22131, 0x3cfb9ac6, 0x3d02a56c, 0x3d0798df, 0x3d0ca7e7, 0x3d11d2b0, 0x3d171965, 0x3d1c7c31, 0x3d21fb3c, 0x3d2796b2, 0x3d2d4ebe, 0x3d332384, 0x3d39152e, 0x3d3f23e6, 0x3d454fd4, 0x3d4b991f,
    0x3d51ffef, 0x3d58846a, 0x3d5f26b7, 0x3d65e6fe, 0x3d6cc564, 0x3d73c20f, 0x3d7add25, 0x3d810b66, 0x3d84b795, 0x3d887330, 0x3d8c3e4a, 0x3d9018f6, 0x3d940345, 0x3d97fd4a, 0x3d9c0716, 0x3da020bb,
    0x3da44a4b, 0x3da883d7, 0x3daccd70, 0x3db12728, 0x3db59110, 0x3dba0b38, 0x3dbe95b5, 0x3dc33092, 0x3dc7dbe2, 0x3dcc97b6, 0x3dd1641f, 0x3dd6412c, 0x3ddb2eef, 0x3de02d77, 0x3de53cd5, 0x3dea5d19,
    0x3def8e55, 0x3df4d093, 0x3dfa23e8, 0x3dff8861, 0x3e027f07, 0x3e054282, 0x3e080ea5, 0x3e0ae379, 0x3e0dc107, 0x3e10a755, 0x3e13966c, 0x3e168e53, 0x3e198f11, 0x3e1c98ae, 0x3e1fab32, 0x3e22c6a3,
    0x3e25eb09, 0x3e29186c, 0x3e2c4ed2, 0x3e2f8e45, 0x3e32d6c8, 0x3e362865, 0x3e398322, 0x3e3ce706, 0x3e405419, 0x3e43ca62, 0x3e4749e8, 0x3e4ad2b1, 0x3e4e64c6, 0x3e52002b, 0x3e55a4e9, 0x3e595307,
    0x3e5d0a8b, 0x3e60cb7c, 0x3e6495e0, 0x3e6869bf, 0x3e6c4720, 0x3e702e08, 0x3e741e7f, 0x3e78188c, 0x3e7c1c38, 0x3e8014c2, 0x3e82203c, 0x3e84308d, 0x3e8645ba, 0x3e885fc5, 0x3e8a7eb2, 0x3e8ca283,
    0x3e8ecb3d, 0x3e90f8e1, 0x3e932b74, 0x3e9562f8, 0x3e979f71, 0x3e99e0e2, 0x3e9c274e, 0x3e9e72b7, 0x3ea0c322, 0x3ea31892, 0x3ea57308, 0x3ea7d289, 0x3eaa3718, 0x3eaca0b7, 0x3eaf0f69, 0x3eb18333,
    0x3eb3fc16, 0x3eb67a15, 0x3eb8fd34, 0x3ebb8576, 0x3ebe12e1, 0x3ec0a571, 0x3ec33d2d, 0x3ec5da17, 0x3ec87c33, 0x3ecb2383, 0x3ecdd00b, 0x3ed081cd, 0x3ed338cc, 0x3ed5f50b, 0x3ed8b68d, 0x3edb7d54,
    0x3ede4965, 0x3ee11ac1, 0x3ee3f16b, 0x3ee6cd67, 0x3ee9aeb6, 0x3eec955d, 0x3eef815d, 0x3ef272ba, 0x3ef56976, 0x3ef86594, 0x3efb6717, 0x3efe6e02, 0x3f00bd2b, 0x3f02460c, 0x3f03d1a5, 0x3f055ff8,
    0x3f06f106, 0x3f0884cf, 0x3f0a1b57, 0x3f0bb49d, 0x3f0d50a2, 0x3f0eef69, 0x3f1090f2, 0x3f123540, 0x3f13dc53, 0x3f15862d, 0x3f1732cf, 0x3f18e23b, 0x3f1a9471, 0x3f1c4973, 0x3f1e0143, 0x3f1fbbe1,
    0x3f217950, 0x3f23398f, 0x3f24fca2, 0x3f26c288, 0x3f288b43, 0x3f2a56d5, 0x3f2c253f, 0x3f2df681, 0x3f2fca9e, 0x3f31a197, 0x3f337b6c, 0x3f355820, 0x3f3737b3, 0x3f391a26, 0x3f3aff7e, 0x3f3ce7b7,
    0x3f3ed2d4, 0x3f40c0d6, 0x3f42b1c0, 0x3f44a592, 0x3f469c4d, 0x3f4895f3, 0x3f4a9284, 0x3f4c9203, 0x3f4e9470, 0x3f5099cd, 0x3f52a21a, 0x3f54ad59, 0x3f56bb8c, 0x3f58ccb3, 0x3f5ae0cf, 0x3f5cf7e2,
    0x3f5f11ee, 0x3f612ef2, 0x3f634eef, 0x3f6571ec, 0x3f6797e1, 0x3f69c0d8, 0x3f6beccb, 0x3f6e1bc2, 0x3f704db6, 0x3f7282b1, 0x3f74baae, 0x3f76f5b3, 0x3f7933b9, 0x3f7b74cb, 0x3f7db8e0, 0x3f800000,
};

// order must match SWR_FORMAT
const SWR_FORMAT_INFO gFormatInfo[] = {
    // R32G32B32A32_FLOAT (0x0)
    {
        "R32G32B32A32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 32, 32, 32, 32 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32A32_SINT (0x1)
    {
        "R32G32B32A32_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 32, 32, 32, 32 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32A32_UINT (0x2)
    {
        "R32G32B32A32_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 32, 32, 32, 32 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x3 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R32G32B32X32_FLOAT (0x6)
    {
        "R32G32B32X32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 32, 32, 32, 32 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32A32_SSCALED (0x7)
    {
        "R32G32B32A32_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 32, 32, 32, 32 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32A32_USCALED (0x8)
    {
        "R32G32B32A32_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 32, 32, 32, 32 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x9 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xc (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xd (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xe (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xf (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x10 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x11 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x14 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x18 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x19 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x20 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x21 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x22 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x23 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x24 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x25 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x26 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x27 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x28 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x29 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x2a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x2b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x2c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x2d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x2e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x2f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x30 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x31 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x32 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x33 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x34 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x35 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x36 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x37 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x38 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x39 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x3a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x3b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x3c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x3d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x3e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x3f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R32G32B32_FLOAT (0x40)
    {
        "R32G32B32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 32, 32, 32, 0 }, // Bits per component
        96, // Bits per element
        12, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32_SINT (0x41)
    {
        "R32G32B32_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 32, 32, 32, 0 }, // Bits per component
        96, // Bits per element
        12, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32_UINT (0x42)
    {
        "R32G32B32_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 32, 32, 32, 0 }, // Bits per component
        96, // Bits per element
        12, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x43 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x44 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R32G32B32_SSCALED (0x45)
    {
        "R32G32B32_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 32, 32, 32, 0 }, // Bits per component
        96, // Bits per element
        12, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32B32_USCALED (0x46)
    {
        "R32G32B32_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 32, 32, 32, 0 }, // Bits per component
        96, // Bits per element
        12, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x47 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x48 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x49 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x4f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x50 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x51 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x52 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x53 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x54 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x55 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x56 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x57 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x58 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x59 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x5f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x60 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x61 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x62 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x63 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x64 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x65 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x66 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x67 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x68 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x69 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x6a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x6b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x6c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x6d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x6e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x6f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x70 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x71 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x72 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x73 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x74 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x75 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x76 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x77 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x78 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x79 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x7a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x7b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x7c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x7d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x7e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x7f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R16G16B16A16_UNORM (0x80)
    {
        "R16G16B16A16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16A16_SNORM (0x81)
    {
        "R16G16B16A16_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16A16_SINT (0x82)
    {
        "R16G16B16A16_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16A16_UINT (0x83)
    {
        "R16G16B16A16_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16A16_FLOAT (0x84)
    {
        "R16G16B16A16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32_FLOAT (0x85)
    {
        "R32G32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32_SINT (0x86)
    {
        "R32G32_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32_UINT (0x87)
    {
        "R32G32_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32_FLOAT_X8X24_TYPELESS (0x88)
    {
        "R32_FLOAT_X8X24_TYPELESS",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // X32_TYPELESS_G8X24_UINT (0x89)
    {
        "X32_TYPELESS_G8X24_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // L32A32_FLOAT (0x8a)
    {
        "L32A32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0x8b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x8c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x8d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R16G16B16X16_UNORM (0x8e)
    {
        "R16G16B16X16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16X16_FLOAT (0x8f)
    {
        "R16G16B16X16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x90 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // L32X32_FLOAT (0x91)
    {
        "L32X32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // I32X32_FLOAT (0x92)
    {
        "I32X32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // R16G16B16A16_SSCALED (0x93)
    {
        "R16G16B16A16_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16A16_USCALED (0x94)
    {
        "R16G16B16A16_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 16, 16, 16, 16 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32_SSCALED (0x95)
    {
        "R32G32_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32G32_USCALED (0x96)
    {
        "R32G32_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x97 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R32_FLOAT_X8X24_TYPELESS_LD (0x98)
    {
        "R32_FLOAT_X8X24_TYPELESS_LD",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 32, 32, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x99 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x9a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x9b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x9c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x9d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x9e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x9f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa0 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa1 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa2 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa3 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa4 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa6 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa7 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa8 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xa9 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xaa (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xab (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xac (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xad (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xae (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xaf (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb0 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb1 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb2 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb3 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb4 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb6 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb7 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb8 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xb9 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xba (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xbb (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xbc (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xbd (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xbe (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xbf (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // B8G8R8A8_UNORM (0xc0)
    {
        "B8G8R8A8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B8G8R8A8_UNORM_SRGB (0xc1)
    {
        "B8G8R8A8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R10G10B10A2_UNORM (0xc2)
    {
        "R10G10B10A2_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R10G10B10A2_UNORM_SRGB (0xc3)
    {
        "R10G10B10A2_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R10G10B10A2_UINT (0xc4)
    {
        "R10G10B10A2_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0xc5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xc6 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R8G8B8A8_UNORM (0xc7)
    {
        "R8G8B8A8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8A8_UNORM_SRGB (0xc8)
    {
        "R8G8B8A8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8A8_SNORM (0xc9)
    {
        "R8G8B8A8_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8A8_SINT (0xca)
    {
        "R8G8B8A8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8A8_UINT (0xcb)
    {
        "R8G8B8A8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_UNORM (0xcc)
    {
        "R16G16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 1.0f / 65535.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_SNORM (0xcd)
    {
        "R16G16_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 32767.0f, 1.0f / 32767.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_SINT (0xce)
    {
        "R16G16_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_UINT (0xcf)
    {
        "R16G16_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_FLOAT (0xd0)
    {
        "R16G16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_UNORM (0xd1)
    {
        "B10G10R10A2_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_UNORM_SRGB (0xd2)
    {
        "B10G10R10A2_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R11G11B10_FLOAT (0xd3)
    {
        "R11G11B10_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 11, 11, 10, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0xd4 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xd5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R32_SINT (0xd6)
    {
        "R32_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32_UINT (0xd7)
    {
        "R32_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32_FLOAT (0xd8)
    {
        "R32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R24_UNORM_X8_TYPELESS (0xd9)
    {
        "R24_UNORM_X8_TYPELESS",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 24, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 16777215.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // X24_TYPELESS_G8_UINT (0xda)
    {
        "X24_TYPELESS_G8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 1, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0xdb (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R24_UNORM_X8_TYPELESS_LD (0xdc)
    {
        "R24_UNORM_X8_TYPELESS_LD",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 24, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 16777215.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // L32_UNORM (0xdd)
    {
        "L32_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 4294967295.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0xde (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // L16A16_UNORM (0xdf)
    {
        "L16A16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 1.0f / 65535.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // I24X8_UNORM (0xe0)
    {
        "I24X8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 24, 8, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 16777215.0f, 1.0f / 255.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L24X8_UNORM (0xe1)
    {
        "L24X8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 24, 8, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 16777215.0f, 1.0f / 255.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0xe2 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // I32_FLOAT (0xe3)
    {
        "I32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L32_FLOAT (0xe4)
    {
        "L32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // A32_FLOAT (0xe5)
    {
        "A32_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 3, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0xe6 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xe7 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xe8 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // B8G8R8X8_UNORM (0xe9)
    {
        "B8G8R8X8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B8G8R8X8_UNORM_SRGB (0xea)
    {
        "B8G8R8X8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8X8_UNORM (0xeb)
    {
        "R8G8B8X8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8X8_UNORM_SRGB (0xec)
    {
        "R8G8B8X8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R9G9B9E5_SHAREDEXP (0xed)
    {
        "R9G9B9E5_SHAREDEXP",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 9, 9, 9, 5 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10X2_UNORM (0xee)
    {
        "B10G10R10X2_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0xef (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // L16A16_FLOAT (0xf0)
    {
        "L16A16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0xf1 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xf2 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R10G10B10X2_USCALED (0xf3)
    {
        "R10G10B10X2_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8A8_SSCALED (0xf4)
    {
        "R8G8B8A8_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8A8_USCALED (0xf5)
    {
        "R8G8B8A8_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_SSCALED (0xf6)
    {
        "R16G16_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16_USCALED (0xf7)
    {
        "R16G16_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 16, 16, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32_SSCALED (0xf8)
    {
        "R32_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R32_USCALED (0xf9)
    {
        "R32_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 32, 0, 0, 0 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0xfa (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xfb (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xfc (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xfd (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xfe (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0xff (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // B5G6R5_UNORM (0x100)
    {
        "B5G6R5_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 0 }, // Swizzle
        { 5, 6, 5, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B5G6R5_UNORM_SRGB (0x101)
    {
        "B5G6R5_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 0 }, // Swizzle
        { 5, 6, 5, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        3, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 31.0f, 1.0f / 63.0f, 1.0f / 31.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B5G5R5A1_UNORM (0x102)
    {
        "B5G5R5A1_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 5, 5, 5, 1 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B5G5R5A1_UNORM_SRGB (0x103)
    {
        "B5G5R5A1_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 5, 5, 5, 1 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B4G4R4A4_UNORM (0x104)
    {
        "B4G4R4A4_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 4, 4, 4, 4 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B4G4R4A4_UNORM_SRGB (0x105)
    {
        "B4G4R4A4_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 4, 4, 4, 4 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f, 1.0f / 15.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8_UNORM (0x106)
    {
        "R8G8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8_SNORM (0x107)
    {
        "R8G8_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 1.0f / 127.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8_SINT (0x108)
    {
        "R8G8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8_UINT (0x109)
    {
        "R8G8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_UNORM (0x10a)
    {
        "R16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_SNORM (0x10b)
    {
        "R16_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 32767.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_SINT (0x10c)
    {
        "R16_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_UINT (0x10d)
    {
        "R16_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_FLOAT (0x10e)
    {
        "R16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x10f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x110 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // I16_UNORM (0x111)
    {
        "I16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L16_UNORM (0x112)
    {
        "L16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // A16_UNORM (0x113)
    {
        "A16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 3, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // L8A8_UNORM (0x114)
    {
        "L8A8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // I16_FLOAT (0x115)
    {
        "I16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L16_FLOAT (0x116)
    {
        "L16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // A16_FLOAT (0x117)
    {
        "A16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 3, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // L8A8_UNORM_SRGB (0x118)
    {
        "L8A8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0x119 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // B5G5R5X1_UNORM (0x11a)
    {
        "B5G5R5X1_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 5, 5, 5, 1 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B5G5R5X1_UNORM_SRGB (0x11b)
    {
        "B5G5R5X1_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNUSED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 5, 5, 5, 1 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        4, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 31.0f, 1.0f / 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8_SSCALED (0x11c)
    {
        "R8G8_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8_USCALED (0x11d)
    {
        "R8G8_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_SSCALED (0x11e)
    {
        "R16_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16_USCALED (0x11f)
    {
        "R16_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 16, 0, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x120 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x121 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x122 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x123 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x124 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x125 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // L8A8_UINT (0x126)
    {
        "L8A8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L8A8_SINT (0x127)
    {
        "L8A8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 3, 0, 0 }, // Swizzle
        { 8, 8, 0, 0 }, // Bits per component
        16, // Bits per element
        2, // Bytes per element
        2, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0x128 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x129 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x12f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x130 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x131 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x132 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x133 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x134 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x135 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x136 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x137 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x138 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x139 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x13f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R8_UNORM (0x140)
    {
        "R8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8_SNORM (0x141)
    {
        "R8_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8_SINT (0x142)
    {
        "R8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8_UINT (0x143)
    {
        "R8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // A8_UNORM (0x144)
    {
        "A8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 3, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // I8_UNORM (0x145)
    {
        "I8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L8_UNORM (0x146)
    {
        "L8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0x147 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x148 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R8_SSCALED (0x149)
    {
        "R8_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8_USCALED (0x14a)
    {
        "R8_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x14b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // L8_UNORM_SRGB (0x14c)
    {
        "L8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0x14d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x14e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x14f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x150 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x151 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // L8_UINT (0x152)
    {
        "L8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // L8_SINT (0x153)
    {
        "L8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // I8_UINT (0x154)
    {
        "I8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // I8_SINT (0x155)
    {
        "I8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        8, // Bits per element
        1, // Bytes per element
        1, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 0, 0, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        true, // isLuminance
    },
    // 0x156 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x157 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x158 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x159 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x15f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x160 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x161 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x162 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x163 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x164 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x165 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x166 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x167 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x168 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x169 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x16f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x170 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x171 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x172 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x173 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x174 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x175 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x176 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x177 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x178 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x179 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17a (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17b (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17c (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17d (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x17f (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x180 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x181 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x182 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // YCRCB_SWAPUVY (0x183)
    {
        "YCRCB_SWAPUVY",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        true, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        2, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x184 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x185 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // BC1_UNORM (0x186)
    {
        "BC1_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC2_UNORM (0x187)
    {
        "BC2_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC3_UNORM (0x188)
    {
        "BC3_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC4_UNORM (0x189)
    {
        "BC4_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC5_UNORM (0x18a)
    {
        "BC5_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC1_UNORM_SRGB (0x18b)
    {
        "BC1_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        1, // Num components
        true, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC2_UNORM_SRGB (0x18c)
    {
        "BC2_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        true, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC3_UNORM_SRGB (0x18d)
    {
        "BC3_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        true, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // 0x18e (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // YCRCB_SWAPUV (0x18f)
    {
        "YCRCB_SWAPUV",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 8, 8, 8, 8 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        true, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        2, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x190 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x191 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x192 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R8G8B8_UNORM (0x193)
    {
        "R8G8B8_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8_SNORM (0x194)
    {
        "R8G8B8_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 1.0f / 127.0f, 1.0f / 127.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8_SSCALED (0x195)
    {
        "R8G8B8_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8_USCALED (0x196)
    {
        "R8G8B8_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x197 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x198 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // BC4_SNORM (0x199)
    {
        "BC4_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        64, // Bits per element
        8, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC5_SNORM (0x19a)
    {
        "BC5_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // R16G16B16_FLOAT (0x19b)
    {
        "R16G16B16_FLOAT",
        { SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_FLOAT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16_UNORM (0x19c)
    {
        "R16G16B16_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 65535.0f, 1.0f / 65535.0f, 1.0f / 65535.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16_SNORM (0x19d)
    {
        "R16G16B16_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / 32767.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16_SSCALED (0x19e)
    {
        "R16G16B16_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16_USCALED (0x19f)
    {
        "R16G16B16_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x1a0 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // BC6H_SF16 (0x1a1)
    {
        "BC6H_SF16",
        { SWR_TYPE_SNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 127.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC7_UNORM (0x1a2)
    {
        "BC7_UNORM",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC7_UNORM_SRGB (0x1a3)
    {
        "BC7_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        true, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // BC6H_UF16 (0x1a4)
    {
        "BC6H_UF16",
        { SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 0, 0, 0 }, // Swizzle
        { 8, 0, 0, 0 }, // Bits per component
        128, // Bits per element
        16, // Bytes per element
        1, // Num components
        false, // isSRGB
        true, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 0, 0, 0 }, // To float scale factor
        4, // bcWidth
        4, // bcHeight
        false, // isLuminance
    },
    // 0x1a5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1a6 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1a7 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R8G8B8_UNORM_SRGB (0x1a8)
    {
        "R8G8B8_UNORM_SRGB",
        { SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNORM, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        true, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x1a9 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1aa (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1ab (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1ac (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1ad (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1ae (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1af (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R16G16B16_UINT (0x1b0)
    {
        "R16G16B16_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R16G16B16_SINT (0x1b1)
    {
        "R16G16B16_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 16, 16, 16, 0 }, // Bits per component
        48, // Bits per element
        6, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x1b2 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R10G10B10A2_SNORM (0x1b3)
    {
        "R10G10B10A2_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R10G10B10A2_USCALED (0x1b4)
    {
        "R10G10B10A2_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R10G10B10A2_SSCALED (0x1b5)
    {
        "R10G10B10A2_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R10G10B10A2_SINT (0x1b6)
    {
        "R10G10B10A2_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_SNORM (0x1b7)
    {
        "B10G10R10A2_SNORM",
        { SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM, SWR_TYPE_SNORM },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { true, true, true, true }, // Is normalized?
        { 1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 511.0f, 1.0f / 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_USCALED (0x1b8)
    {
        "B10G10R10A2_USCALED",
        { SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED, SWR_TYPE_USCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_SSCALED (0x1b9)
    {
        "B10G10R10A2_SSCALED",
        { SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED, SWR_TYPE_SSCALED },
        { 0, 0, 0, 0x3f800000 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_UINT (0x1ba)
    {
        "B10G10R10A2_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // B10G10R10A2_SINT (0x1bb)
    {
        "B10G10R10A2_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 2, 1, 0, 3 }, // Swizzle
        { 10, 10, 10, 2 }, // Bits per component
        32, // Bits per element
        4, // Bytes per element
        4, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 1.0f }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // 0x1bc (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1bd (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1be (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1bf (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c0 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c1 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c2 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c3 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c4 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c5 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c6 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // 0x1c7 (Padding)
    {
        "UNKNOWN",
        { SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0, 0, 0, false, false, false,
        { false, false, false, false },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        1, 1, false    },
    // R8G8B8_UINT (0x1c8)
    {
        "R8G8B8_UINT",
        { SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UINT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
    // R8G8B8_SINT (0x1c9)
    {
        "R8G8B8_SINT",
        { SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_SINT, SWR_TYPE_UNKNOWN },
        { 0, 0, 0, 0x1 }, // Defaults for missing components
        { 0, 1, 2, 0 }, // Swizzle
        { 8, 8, 8, 0 }, // Bits per component
        24, // Bits per element
        3, // Bytes per element
        3, // Num components
        false, // isSRGB
        false, // isBC
        false, // isSubsampled
        { false, false, false, false }, // Is normalized?
        { 1.0f, 1.0f, 1.0f, 0 }, // To float scale factor
        1, // bcWidth
        1, // bcHeight
        false, // isLuminance
    },
};
