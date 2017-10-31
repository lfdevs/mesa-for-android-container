/*
 * Copyright © 2014-2017 Broadcom
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

/**
 * @file vc5_formats.c
 *
 * Contains the table and accessors for VC5 texture and render target format
 * support.
 *
 * The hardware has limited support for texture formats, and extremely limited
 * support for render target formats.  As a result, we emulate other formats
 * in our shader code, and this stores the table for doing so.
 */

#include "util/u_format.h"
#include "util/macros.h"

#include "vc5_context.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"

#define OUTPUT_IMAGE_FORMAT_NO 255

struct vc5_format {
        /** Set if the pipe format is defined in the table. */
        bool present;

        /** One of V3D33_OUTPUT_IMAGE_FORMAT_*, or OUTPUT_IMAGE_FORMAT_NO */
        uint8_t rt_type;

        /** One of V3D33_TEXTURE_DATA_FORMAT_*. */
        uint8_t tex_type;

        /**
         * Swizzle to apply to the RGBA shader output for storing to the tile
         * buffer, to the RGBA tile buffer to produce shader input (for
         * blending), and for turning the rgba8888 texture sampler return
         * value into shader rgba values.
         */
        uint8_t swizzle[4];

        /* Whether the return value is 16F/I/UI or 32F/I/UI. */
        uint8_t return_size;

        /* If return_size == 32, how many channels are returned by texturing.
         * 16 always returns 2 pairs of 16 bit values.
         */
        uint8_t return_channels;
};

#define SWIZ(x,y,z,w) {          \
        PIPE_SWIZZLE_##x, \
        PIPE_SWIZZLE_##y, \
        PIPE_SWIZZLE_##z, \
        PIPE_SWIZZLE_##w  \
}

#define FORMAT(pipe, rt, tex, swiz, return_size, return_channels)       \
        [PIPE_FORMAT_##pipe] = {                                        \
                true,                                                   \
                OUTPUT_IMAGE_FORMAT_##rt,                               \
                TEXTURE_DATA_FORMAT_##tex,                              \
                swiz,                                                   \
                return_size,                                            \
                return_channels,                                        \
        }

#define SWIZ_X001	SWIZ(X, 0, 0, 1)
#define SWIZ_XY01	SWIZ(X, Y, 0, 1)
#define SWIZ_XYZ1	SWIZ(X, Y, Z, 1)
#define SWIZ_XYZW	SWIZ(X, Y, Z, W)
#define SWIZ_YZWX	SWIZ(Y, Z, W, X)
#define SWIZ_YZW1	SWIZ(Y, Z, W, 1)
#define SWIZ_ZYXW	SWIZ(Z, Y, X, W)
#define SWIZ_ZYX1	SWIZ(Z, Y, X, 1)
#define SWIZ_XXXY	SWIZ(X, X, X, Y)
#define SWIZ_XXX1	SWIZ(X, X, X, 1)
#define SWIZ_XXXX	SWIZ(X, X, X, X)
#define SWIZ_000X	SWIZ(0, 0, 0, X)

static const struct vc5_format vc5_format_table[] = {
        FORMAT(B8G8R8A8_UNORM,    RGBA8,        RGBA8,       SWIZ_ZYXW, 16, 0),
        FORMAT(B8G8R8X8_UNORM,    RGBX8,        RGBA8,       SWIZ_ZYX1, 16, 0),
        FORMAT(B8G8R8A8_SRGB,     SRGB8_ALPHA8, RGBA8,       SWIZ_ZYXW, 16, 0),
        FORMAT(B8G8R8X8_SRGB,     SRGBX8,       RGBA8,       SWIZ_ZYX1, 16, 0),
        FORMAT(R8G8B8A8_UNORM,    RGBA8,        RGBA8,       SWIZ_XYZW, 16, 0),
        FORMAT(R8G8B8X8_UNORM,    RGBX8,        RGBA8,       SWIZ_XYZ1, 16, 0),
        FORMAT(R8G8B8A8_SNORM,    NO,           RGBA8_SNORM, SWIZ_XYZW, 16, 0),
        FORMAT(R8G8B8X8_SNORM,    NO,           RGBA8_SNORM, SWIZ_XYZ1, 16, 0),
        FORMAT(B10G10R10A2_UNORM, RGB10_A2,     RGB10_A2,    SWIZ_ZYXW, 16, 0),

        FORMAT(B4G4R4A4_UNORM,    ABGR4444,     RGBA4,       SWIZ_YZWX, 16, 0),
        FORMAT(B4G4R4X4_UNORM,    ABGR4444,     RGBA4,       SWIZ_YZW1, 16, 0),

        FORMAT(A1B5G5R5_UNORM,    ABGR1555,     RGB5_A1,     SWIZ_XYZW, 16, 0),
        FORMAT(X1B5G5R5_UNORM,    ABGR1555,     RGB5_A1,     SWIZ_XYZ1, 16, 0),
        FORMAT(B5G6R5_UNORM,      BGR565,       RGB565,      SWIZ_XYZ1, 16, 0),

        FORMAT(R8_UNORM,          R8,           R8,          SWIZ_X001, 16, 0),
        FORMAT(R8_SNORM,          NO,           R8_SNORM,    SWIZ_X001, 16, 0),
        FORMAT(R8G8_UNORM,        RG8,          RG8,         SWIZ_XY01, 16, 0),
        FORMAT(R8G8_SNORM,        NO,           RG8_SNORM,   SWIZ_XY01, 16, 0),

        FORMAT(R16_UNORM,         NO,           R16,         SWIZ_X001, 32, 1),
        FORMAT(R16_SNORM,         NO,           R16_SNORM,   SWIZ_X001, 32, 1),
        FORMAT(R16_FLOAT,         R16F,         R16F,        SWIZ_X001, 16, 0),
        FORMAT(R32_FLOAT,         R32F,         R32F,        SWIZ_X001, 32, 1),

        FORMAT(R16G16_UNORM,      NO,           RG16,        SWIZ_XY01, 32, 2),
        FORMAT(R16G16_SNORM,      NO,           RG16_SNORM,  SWIZ_XY01, 32, 2),
        FORMAT(R16G16_FLOAT,      RG16F,        RG16F,       SWIZ_XY01, 16, 0),
        FORMAT(R32G32_FLOAT,      RG32F,        RG32F,       SWIZ_XY01, 32, 2),

        FORMAT(R16G16B16A16_UNORM, NO,          RGBA16,      SWIZ_XYZW, 32, 4),
        FORMAT(R16G16B16A16_SNORM, NO,          RGBA16_SNORM, SWIZ_XYZW, 32, 4),
        FORMAT(R16G16B16A16_FLOAT, RGBA16F,     RGBA16F,     SWIZ_XYZW, 16, 0),
        FORMAT(R32G32B32A32_FLOAT, RGBA32F,     RGBA32F,     SWIZ_XYZW, 32, 4),

        /* If we don't have L/A/LA16, mesa/st will fall back to RGBA16. */
        FORMAT(L16_UNORM,         NO,           R16,         SWIZ_XXX1, 32, 1),
        FORMAT(L16_SNORM,         NO,           R16_SNORM,   SWIZ_XXX1, 32, 1),
        FORMAT(I16_UNORM,         NO,           R16,         SWIZ_XXXX, 32, 1),
        FORMAT(I16_SNORM,         NO,           R16_SNORM,   SWIZ_XXXX, 32, 1),
        FORMAT(A16_UNORM,         NO,           R16,         SWIZ_000X, 32, 1),
        FORMAT(A16_SNORM,         NO,           R16_SNORM,   SWIZ_000X, 32, 1),
        FORMAT(L16A16_UNORM,      NO,           RG16,        SWIZ_XXXY, 32, 2),
        FORMAT(L16A16_SNORM,      NO,           RG16_SNORM,  SWIZ_XXXY, 32, 2),

        FORMAT(A8_UNORM,          NO,           R8,          SWIZ_000X, 16, 0),
        FORMAT(L8_UNORM,          NO,           R8,          SWIZ_XXX1, 16, 0),
        FORMAT(I8_UNORM,          NO,           R8,          SWIZ_XXXX, 16, 0),
        FORMAT(L8A8_UNORM,        NO,           RG8,         SWIZ_XXXY, 16, 0),

        FORMAT(R8_SINT,           R8I,          S8,          SWIZ_X001, 16, 0),
        FORMAT(R8_UINT,           R8UI,         S8,          SWIZ_X001, 16, 0),
        FORMAT(R8G8_SINT,         RG8I,         S16,         SWIZ_XY01, 16, 0),
        FORMAT(R8G8_UINT,         RG8UI,        S16,         SWIZ_XY01, 16, 0),
        FORMAT(R8G8B8A8_SINT,     RGBA8I,       R32F,        SWIZ_XYZW, 16, 0),
        FORMAT(R8G8B8A8_UINT,     RGBA8UI,      R32F,        SWIZ_XYZW, 16, 0),

        FORMAT(R16_SINT,          R16I,         S16,         SWIZ_X001, 16, 0),
        FORMAT(R16_UINT,          R16UI,        S16,         SWIZ_X001, 16, 0),
        FORMAT(R16G16_SINT,       RG16I,        R32F,        SWIZ_XY01, 16, 0),
        FORMAT(R16G16_UINT,       RG16UI,       R32F,        SWIZ_XY01, 16, 0),
        FORMAT(R16G16B16A16_SINT, RGBA16I,      RG32F,       SWIZ_XYZW, 16, 0),
        FORMAT(R16G16B16A16_UINT, RGBA16UI,     RG32F,       SWIZ_XYZW, 16, 0),

        FORMAT(R32_SINT,          R32I,         R32F,        SWIZ_X001, 16, 0),
        FORMAT(R32_UINT,          R32UI,        R32F,        SWIZ_X001, 16, 0),
        FORMAT(R32G32_SINT,       RG32I,        RG32F,       SWIZ_XY01, 16, 0),
        FORMAT(R32G32_UINT,       RG32UI,       RG32F,       SWIZ_XY01, 16, 0),
        FORMAT(R32G32B32A32_SINT, RGBA32I,      RGBA32F,     SWIZ_XYZW, 16, 0),
        FORMAT(R32G32B32A32_UINT, RGBA32UI,     RGBA32F,     SWIZ_XYZW, 16, 0),

        FORMAT(A8_SINT,           R8I,          S8,          SWIZ_000X, 16, 0),
        FORMAT(A8_UINT,           R8UI,         S8,          SWIZ_000X, 16, 0),
        FORMAT(A16_SINT,          R16I,         S16,         SWIZ_000X, 16, 0),
        FORMAT(A16_UINT,          R16UI,        S16,         SWIZ_000X, 16, 0),
        FORMAT(A32_SINT,          R32I,         R32F,        SWIZ_000X, 16, 0),
        FORMAT(A32_UINT,          R32UI,        R32F,        SWIZ_000X, 16, 0),

        FORMAT(R11G11B10_FLOAT,   R11F_G11F_B10F, R11F_G11F_B10F, SWIZ_XYZW, 16, 0),
        FORMAT(R9G9B9E5_FLOAT,    NO,           RGB9_E5,     SWIZ_XYZW, 16, 0),

        FORMAT(S8_UINT_Z24_UNORM, DEPTH24_STENCIL8, DEPTH24_X8, SWIZ_X001, 32, 1),
        FORMAT(X8Z24_UNORM,       DEPTH_COMPONENT24, DEPTH24_X8, SWIZ_X001, 32, 1),
        FORMAT(S8X24_UINT,        NO,           R32F,        SWIZ_X001, 32, 1),
        FORMAT(Z32_FLOAT,         DEPTH_COMPONENT32F, R32F, SWIZ_X001, 32, 1),
        FORMAT(Z16_UNORM,         DEPTH_COMPONENT16,  DEPTH_COMP16, SWIZ_X001, 32, 1),

        /* Pretend we support this, but it'll be separate Z32F depth and S8. */
        FORMAT(Z32_FLOAT_S8X24_UINT, DEPTH_COMPONENT32F, R32F, SWIZ_X001, 32, 1),

        FORMAT(ETC2_RGB8,         NO,           RGB8_ETC2,   SWIZ_XYZ1, 16, 0),
        FORMAT(ETC2_SRGB8,        NO,           RGB8_ETC2,   SWIZ_XYZ1, 16, 0),
        FORMAT(ETC2_RGB8A1,       NO,           RGB8_PUNCHTHROUGH_ALPHA1, SWIZ_XYZW, 16, 0),
        FORMAT(ETC2_SRGB8A1,      NO,           RGB8_PUNCHTHROUGH_ALPHA1, SWIZ_XYZW, 16, 0),
        FORMAT(ETC2_RGBA8,        NO,           RGBA8_ETC2_EAC, SWIZ_XYZW, 16, 0),
        FORMAT(ETC2_R11_UNORM,    NO,           R11_EAC,     SWIZ_X001, 16, 0),
        FORMAT(ETC2_R11_SNORM,    NO,           SIGNED_R11_EAC, SWIZ_X001, 16, 0),
        FORMAT(ETC2_RG11_UNORM,   NO,           RG11_EAC,    SWIZ_XY01, 16, 0),
        FORMAT(ETC2_RG11_SNORM,   NO,           SIGNED_RG11_EAC, SWIZ_XY01, 16, 0),

        FORMAT(DXT1_RGB,          NO,           BC1,         SWIZ_XYZ1, 16, 0),
        FORMAT(DXT3_RGBA,         NO,           BC2,         SWIZ_XYZ1, 16, 0),
        FORMAT(DXT5_RGBA,         NO,           BC3,         SWIZ_XYZ1, 16, 0),
};

static const struct vc5_format *
get_format(enum pipe_format f)
{
        if (f >= ARRAY_SIZE(vc5_format_table) ||
            !vc5_format_table[f].present)
                return NULL;
        else
                return &vc5_format_table[f];
}

bool
vc5_rt_format_supported(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);

        if (!vf)
                return false;

        return vf->rt_type != OUTPUT_IMAGE_FORMAT_NO;
}

uint8_t
vc5_get_rt_format(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);

        if (!vf)
                return 0;

        return vf->rt_type;
}

bool
vc5_tex_format_supported(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);

        return vf != NULL;
}

uint8_t
vc5_get_tex_format(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);

        if (!vf)
                return 0;

        return vf->tex_type;
}

uint8_t
vc5_get_tex_return_size(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);

        if (!vf)
                return 0;

        return vf->return_size;
}

uint8_t
vc5_get_tex_return_channels(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);

        if (!vf)
                return 0;

        return vf->return_channels;
}

const uint8_t *
vc5_get_format_swizzle(enum pipe_format f)
{
        const struct vc5_format *vf = get_format(f);
        static const uint8_t fallback[] = {0, 1, 2, 3};

        if (!vf)
                return fallback;

        return vf->swizzle;
}

void
vc5_get_internal_type_bpp_for_output_format(uint32_t format,
                                            uint32_t *type,
                                            uint32_t *bpp)
{
        switch (format) {
        case OUTPUT_IMAGE_FORMAT_RGBA8:
        case OUTPUT_IMAGE_FORMAT_RGBX8:
        case OUTPUT_IMAGE_FORMAT_RGB8:
        case OUTPUT_IMAGE_FORMAT_RG8:
        case OUTPUT_IMAGE_FORMAT_R8:
        case OUTPUT_IMAGE_FORMAT_ABGR4444:
        case OUTPUT_IMAGE_FORMAT_BGR565:
        case OUTPUT_IMAGE_FORMAT_ABGR1555:
                *type = INTERNAL_TYPE_8;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA8I:
        case OUTPUT_IMAGE_FORMAT_RG8I:
        case OUTPUT_IMAGE_FORMAT_R8I:
                *type = INTERNAL_TYPE_8I;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA8UI:
        case OUTPUT_IMAGE_FORMAT_RG8UI:
        case OUTPUT_IMAGE_FORMAT_R8UI:
                *type = INTERNAL_TYPE_8UI;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_SRGB8_ALPHA8:
        case OUTPUT_IMAGE_FORMAT_SRGB:
        case OUTPUT_IMAGE_FORMAT_RGB10_A2:
        case OUTPUT_IMAGE_FORMAT_R11F_G11F_B10F:
        case OUTPUT_IMAGE_FORMAT_SRGBX8:
        case OUTPUT_IMAGE_FORMAT_RGBA16F:
                /* Note that sRGB RTs are stored in the tile buffer at 16F,
                 * and the conversion to sRGB happens at tilebuffer
                 * load/store.
                 */
                *type = INTERNAL_TYPE_16F;
                *bpp = INTERNAL_BPP_64;
                break;

        case OUTPUT_IMAGE_FORMAT_RG16F:
        case OUTPUT_IMAGE_FORMAT_R16F:
                *type = INTERNAL_TYPE_16F;
                /* Use 64bpp to make sure the TLB doesn't throw away the alpha
                 * channel before alpha test happens.
                 */
                *bpp = INTERNAL_BPP_64;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA16I:
                *type = INTERNAL_TYPE_16I;
                *bpp = INTERNAL_BPP_64;
                break;
        case OUTPUT_IMAGE_FORMAT_RG16I:
        case OUTPUT_IMAGE_FORMAT_R16I:
                *type = INTERNAL_TYPE_16I;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA16UI:
                *type = INTERNAL_TYPE_16UI;
                *bpp = INTERNAL_BPP_64;
                break;
        case OUTPUT_IMAGE_FORMAT_RG16UI:
        case OUTPUT_IMAGE_FORMAT_R16UI:
                *type = INTERNAL_TYPE_16UI;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA32I:
                *type = INTERNAL_TYPE_32I;
                *bpp = INTERNAL_BPP_128;
                break;
        case OUTPUT_IMAGE_FORMAT_RG32I:
                *type = INTERNAL_TYPE_32I;
                *bpp = INTERNAL_BPP_64;
                break;
        case OUTPUT_IMAGE_FORMAT_R32I:
                *type = INTERNAL_TYPE_32I;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA32UI:
                *type = INTERNAL_TYPE_32UI;
                *bpp = INTERNAL_BPP_128;
                break;
        case OUTPUT_IMAGE_FORMAT_RG32UI:
                *type = INTERNAL_TYPE_32UI;
                *bpp = INTERNAL_BPP_64;
                break;
        case OUTPUT_IMAGE_FORMAT_R32UI:
                *type = INTERNAL_TYPE_32UI;
                *bpp = INTERNAL_BPP_32;
                break;

        case OUTPUT_IMAGE_FORMAT_RGBA32F:
                *type = INTERNAL_TYPE_32F;
                *bpp = INTERNAL_BPP_128;
                break;
        case OUTPUT_IMAGE_FORMAT_RG32F:
                *type = INTERNAL_TYPE_32F;
                *bpp = INTERNAL_BPP_64;
                break;
        case OUTPUT_IMAGE_FORMAT_R32F:
                *type = INTERNAL_TYPE_32F;
                *bpp = INTERNAL_BPP_32;
                break;

        default:
                /* Provide some default values, as we'll be called at RB
                 * creation time, even if an RB with this format isn't
                 * supported.
                 */
                *type = INTERNAL_TYPE_8;
                *bpp = INTERNAL_BPP_32;
                break;
        }
}
