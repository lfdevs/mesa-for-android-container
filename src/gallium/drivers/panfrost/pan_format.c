/*
 * © Copyright 2018 Alyssa Rosenzweig
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "pan_format.h"

/* From panwrap/panwrap-decoder, but we don't want to bring in all those headers */
char *panwrap_format_name(enum mali_format format);

/* Construct a default swizzle based on the number of components */

static unsigned
panfrost_translate_swizzle(enum pipe_swizzle s)
{
        switch (s) {
        case PIPE_SWIZZLE_X:
                return MALI_CHANNEL_RED;

        case PIPE_SWIZZLE_Y:
                return MALI_CHANNEL_GREEN;

        case PIPE_SWIZZLE_Z:
                return MALI_CHANNEL_BLUE;

        case PIPE_SWIZZLE_W:
                return MALI_CHANNEL_ALPHA;

        case PIPE_SWIZZLE_0:
        case PIPE_SWIZZLE_NONE:
                return MALI_CHANNEL_ZERO;

        case PIPE_SWIZZLE_1:
                return MALI_CHANNEL_ONE;

        default:
                assert(0);
                return 0;
        }
}

/* Translate a Gallium swizzle quad to a 12-bit Mali swizzle code */

unsigned
panfrost_translate_swizzle_4(const unsigned char swizzle[4])
{
        unsigned out = 0;

        for (unsigned i = 0; i < 4; ++i) {
                unsigned translated = panfrost_translate_swizzle(swizzle[i]);
                out |= (translated << (3*i));
        }

        return out;
}

unsigned
panfrost_get_default_swizzle(unsigned components)
{
        unsigned char default_swizzles[4][4] = {
                {PIPE_SWIZZLE_X, PIPE_SWIZZLE_0, PIPE_SWIZZLE_0, PIPE_SWIZZLE_1},
                {PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_0, PIPE_SWIZZLE_1},
                {PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_1},
                {PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W},
        };

        assert(components >= 1 && components <= 4);
        return panfrost_translate_swizzle_4(default_swizzles[components - 1]);
}

static unsigned
panfrost_translate_channel_width(unsigned size)
{
        switch (size) {
                case 4:
                        return MALI_CHANNEL_4;
                case 8:
                        return MALI_CHANNEL_8;
                case 16:
                        return MALI_CHANNEL_16;
                case 32:
                        return MALI_CHANNEL_32;
                default:
                        fprintf(stderr, "Unknown width %d\n", size);
                        assert(0);
                        return 0;
        }
}

static unsigned
panfrost_translate_channel_type(unsigned type, unsigned size, bool norm) {
        switch (type) {
                case UTIL_FORMAT_TYPE_UNSIGNED:
                        return norm ? MALI_FORMAT_UNORM : MALI_FORMAT_UINT;

                case UTIL_FORMAT_TYPE_SIGNED:
                        return norm ? MALI_FORMAT_SNORM : MALI_FORMAT_SINT;

                case UTIL_FORMAT_TYPE_FLOAT:
                        if (size == 16) {
                                /* With FLOAT, fp16 */
                                return MALI_FORMAT_SINT;
                        } else if (size == 32) {
                                /* With FLOAT< fp32 */
                                return MALI_FORMAT_UNORM;
                        } else {
                                assert(0);
                                return 0;
                        }

                default:
                        assert(0);
                        return 0;
        }
}

/* Constructs a mali_format satisfying the specified Gallium format
 * description */

enum mali_format
panfrost_find_format(const struct util_format_description *desc)
{
        /* Find first non-VOID channel */
        struct util_format_channel_description chan = desc->channel[0];

        for (unsigned c = 0; c < 4; ++c) {
                if (desc->channel[c].type == UTIL_FORMAT_TYPE_VOID)
                        continue;

                chan = desc->channel[c];
                break;
        }

        /* Check for special formats */
        switch (desc->format) {
		case PIPE_FORMAT_YV12:
		case PIPE_FORMAT_YV16:
		case PIPE_FORMAT_IYUV:
		case PIPE_FORMAT_NV21:
			fprintf(stderr, "YUV format type %s (%d) is not yet supported, but it's probably close to NV12!\n", desc->name, desc->format);
			assert(0);
			break;

		case PIPE_FORMAT_NV12:
			return MALI_NV12;

                case PIPE_FORMAT_R10G10B10X2_UNORM:
                case PIPE_FORMAT_B10G10R10X2_UNORM:
                case PIPE_FORMAT_R10G10B10A2_UNORM:
                case PIPE_FORMAT_B10G10R10A2_UNORM:
                        return MALI_RGB10_A2_UNORM;

                case PIPE_FORMAT_R10G10B10X2_SNORM:
                case PIPE_FORMAT_R10G10B10A2_SNORM:
                case PIPE_FORMAT_B10G10R10A2_SNORM:
                        return MALI_RGB10_A2_SNORM;

                case PIPE_FORMAT_R10G10B10A2_UINT:
                case PIPE_FORMAT_B10G10R10A2_UINT:
                        return MALI_RGB10_A2UI;

                /* TODO: ZS isn't really special case */
                case PIPE_FORMAT_Z32_UNORM:
                        return MALI_Z32_UNORM;

                case PIPE_FORMAT_B5G6R5_UNORM:
                        return MALI_RGB565;

                case PIPE_FORMAT_B5G5R5A1_UNORM:
                        return MALI_RGB5_A1_UNORM;

                case PIPE_FORMAT_A1B5G5R5_UNORM:
                case PIPE_FORMAT_X1B5G5R5_UNORM:
                        /* Not supported - this is backwards from OpenGL! */
                        assert(0);
                        break;

                case PIPE_FORMAT_R32_FIXED:
                        return MALI_R32_FIXED;
                case PIPE_FORMAT_R32G32_FIXED:
                        return MALI_RG32_FIXED;
                case PIPE_FORMAT_R32G32B32_FIXED:
                        return MALI_RGB32_FIXED;
                case PIPE_FORMAT_R32G32B32A32_FIXED:
                        return MALI_RGBA32_FIXED;

                default:
                        /* Fallthrough to default */
                        break;
        }

        /* Formats must match in channel count */
        assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);
        unsigned format = MALI_NR_CHANNELS(desc->nr_channels);

        switch (chan.type) {
                case UTIL_FORMAT_TYPE_UNSIGNED:
                case UTIL_FORMAT_TYPE_SIGNED:
                case UTIL_FORMAT_TYPE_FIXED:
                        /* Channel width */
                        format |= panfrost_translate_channel_width(chan.size);

                        /* Channel type */
                        format |= panfrost_translate_channel_type(chan.type, chan.size, chan.normalized);
                        break;

                case UTIL_FORMAT_TYPE_FLOAT:
                        /* Float formats use a special width and encode width
                         * with type mixed */

                        format |= MALI_CHANNEL_FLOAT;
                        format |= panfrost_translate_channel_type(chan.type, chan.size, chan.normalized);
                        break;

                default:
                        fprintf(stderr, "Unknown format type in %s\n", desc->name);
                        assert(0);
                        break;
        }

        return (enum mali_format) format;
}


