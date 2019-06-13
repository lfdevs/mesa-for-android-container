/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * Copyright 2014 Broadcom
 * Copyright 2018 Alyssa Rosenzweig
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "util/u_video.h"
#include "util/u_screen.h"
#include "util/os_time.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"
#include <xf86drm.h>

#include <fcntl.h>

#include "drm-uapi/drm_fourcc.h"

#include "pan_screen.h"
#include "pan_resource.h"
#include "pan_public.h"
#include "pan_util.h"

#include "pan_context.h"
#include "midgard/midgard_compile.h"

static const struct debug_named_value debug_options[] = {
	{"msgs",      PAN_DBG_MSGS,	"Print debug messages"},
	DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(pan_debug, "PAN_MESA_DEBUG", debug_options, 0)

int pan_debug = 0;

struct panfrost_driver *panfrost_create_drm_driver(int fd);

const char *pan_counters_base = NULL;

static const char *
panfrost_get_name(struct pipe_screen *screen)
{
        return "panfrost";
}

static const char *
panfrost_get_vendor(struct pipe_screen *screen)
{
        return "panfrost";
}

static const char *
panfrost_get_device_vendor(struct pipe_screen *screen)
{
        return "Arm";
}

static int
panfrost_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
        switch (param) {
        case PIPE_CAP_NPOT_TEXTURES:
        case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
        case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
                return 1;

        case PIPE_CAP_SM3:
        case PIPE_CAP_POINT_SPRITE:
                return 1;

        case PIPE_CAP_MAX_RENDER_TARGETS:
        case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
                return 1;

        case PIPE_CAP_OCCLUSION_QUERY:
        case PIPE_CAP_QUERY_TIME_ELAPSED:
        case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
                return 1; /* TODO: Queries */

        case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
        case PIPE_CAP_TEXTURE_SWIZZLE:
                return 1;

        /* TODO: ES3. We expose these caps so we can access higher dEQP
         * tests; in actuality they are nonfunctional */
        case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
                return 4;
        case PIPE_CAP_TGSI_INSTANCEID:
        case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
        case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
        case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
                return 13;

        case PIPE_CAP_BLEND_EQUATION_SEPARATE:
                return 1;

        case PIPE_CAP_INDEP_BLEND_ENABLE:
                return 1;

        case PIPE_CAP_INDEP_BLEND_FUNC:
                return 1;

        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
                return 1;

        case PIPE_CAP_DEPTH_CLIP_DISABLE:
                return 1;

        case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
        case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
                return 16 * 4;

        case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
        case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
                return 1024;

        case PIPE_CAP_MAX_VERTEX_STREAMS:
                return 1;

        case PIPE_CAP_SHADER_STENCIL_EXPORT:
                return 1;

        case PIPE_CAP_SEAMLESS_CUBE_MAP:
        case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
                return 256; /* for GL3 */

        case PIPE_CAP_CONDITIONAL_RENDER:
                return 1;

        case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
        case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
        case PIPE_CAP_VERTEX_COLOR_CLAMPED:
                return 1;

        case PIPE_CAP_GLSL_FEATURE_LEVEL:
                return 330;

        case PIPE_CAP_USER_VERTEX_BUFFERS: /* TODO */
        case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
                return 0;

        case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
        case PIPE_CAP_DOUBLES:
        case PIPE_CAP_INT64:
        case PIPE_CAP_INT64_DIVMOD:
                return 1;

        case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
                return 16;

        case PIPE_CAP_MAX_VERTEX_ELEMENT_SRC_OFFSET:
                return 0xffff;

        case PIPE_CAP_QUERY_TIMESTAMP:
        case PIPE_CAP_CUBE_MAP_ARRAY:
                return 1;

        case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
                return 65536;

        case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
                return 0;

        case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
                return 0;

        case PIPE_CAP_MAX_VIEWPORTS:
                return PIPE_MAX_VIEWPORTS;

        case PIPE_CAP_ENDIANNESS:
                return PIPE_ENDIAN_NATIVE;

        case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
                return 4;

        case PIPE_CAP_TEXTURE_GATHER_SM5:
        case PIPE_CAP_TEXTURE_QUERY_LOD:
        case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
        case PIPE_CAP_SAMPLER_VIEW_TARGET:
        case PIPE_CAP_FAKE_SW_MSAA:
                return 1;

        case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
                return -32;

        case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
                return 31;

        case PIPE_CAP_DRAW_INDIRECT:
                return 1;

        case PIPE_CAP_QUERY_SO_OVERFLOW:
                return 1;

        case PIPE_CAP_VENDOR_ID:
                return 0xFFFFFFFF;

        case PIPE_CAP_DEVICE_ID:
                return 0xFFFFFFFF;

        case PIPE_CAP_ACCELERATED:
                return 1;

        case PIPE_CAP_VIDEO_MEMORY: {
                uint64_t system_memory;

                if (!os_get_total_physical_memory(&system_memory))
                        return 0;

                return (int)(system_memory >> 20);
        }

        case PIPE_CAP_UMA:
                return 1;

        case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
        case PIPE_CAP_CLIP_HALFZ:
        case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
        case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
        case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
        case PIPE_CAP_CULL_DISTANCE:
        case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
        case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
        case PIPE_CAP_CLEAR_TEXTURE:
                return 1;

        case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
                return 4;

        case PIPE_CAP_MAX_VARYINGS:
                return 16;

        default:
                return u_pipe_screen_get_param_defaults(screen, param);
        }
}

static int
panfrost_get_shader_param(struct pipe_screen *screen,
                          enum pipe_shader_type shader,
                          enum pipe_shader_cap param)
{
        if (shader != PIPE_SHADER_VERTEX &&
                        shader != PIPE_SHADER_FRAGMENT) {
                return 0;
        }

        /* this is probably not totally correct.. but it's a start: */
        switch (param) {
        case PIPE_SHADER_CAP_SCALAR_ISA:
                return 0;

        case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
                return 0;
        case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
                return 16384;

        case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
                return 1024;

        case PIPE_SHADER_CAP_MAX_INPUTS:
                return 16;

        case PIPE_SHADER_CAP_MAX_OUTPUTS:
                return shader == PIPE_SHADER_FRAGMENT ? 1 : 8;

        case PIPE_SHADER_CAP_MAX_TEMPS:
                return 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */

        case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
                return 16 * 1024 * sizeof(float);

        case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
                return 4;

        case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
                return 0;

        case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
                return 1;
        case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
                return 0;

        case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
                return 0;

        case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
                return 1;

        case PIPE_SHADER_CAP_SUBROUTINES:
                return 0;

        case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
                return 0;

        case PIPE_SHADER_CAP_INTEGERS:
                return 1;

        case PIPE_SHADER_CAP_INT64_ATOMICS:
        case PIPE_SHADER_CAP_FP16:
        case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
                return 0;

        case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
        case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
                return 16; /* XXX: How many? */

        case PIPE_SHADER_CAP_PREFERRED_IR:
                return PIPE_SHADER_IR_NIR;

        case PIPE_SHADER_CAP_SUPPORTED_IRS:
                return 0;

        case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
                return 32;

        case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
        case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
        case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
        case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
                return 0;

        default:
                fprintf(stderr, "unknown shader param %d\n", param);
                return 0;
        }

        return 0;
}

static float
panfrost_get_paramf(struct pipe_screen *screen, enum pipe_capf param)
{
        switch (param) {
        case PIPE_CAPF_MAX_LINE_WIDTH:

        /* fall-through */
        case PIPE_CAPF_MAX_LINE_WIDTH_AA:
                return 255.0; /* arbitrary */

        case PIPE_CAPF_MAX_POINT_WIDTH:

        /* fall-through */
        case PIPE_CAPF_MAX_POINT_WIDTH_AA:
                return 255.0; /* arbitrary */

        case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
                return 16.0;

        case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
                return 16.0; /* arbitrary */

        default:
                debug_printf("Unexpected PIPE_CAPF %d query\n", param);
                return 0.0;
        }
}

/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static boolean
panfrost_is_format_supported( struct pipe_screen *screen,
                              enum pipe_format format,
                              enum pipe_texture_target target,
                              unsigned sample_count,
                              unsigned storage_sample_count,
                              unsigned bind)
{
        const struct util_format_description *format_desc;

        assert(target == PIPE_BUFFER ||
               target == PIPE_TEXTURE_1D ||
               target == PIPE_TEXTURE_1D_ARRAY ||
               target == PIPE_TEXTURE_2D ||
               target == PIPE_TEXTURE_2D_ARRAY ||
               target == PIPE_TEXTURE_RECT ||
               target == PIPE_TEXTURE_3D ||
               target == PIPE_TEXTURE_CUBE ||
               target == PIPE_TEXTURE_CUBE_ARRAY);

        format_desc = util_format_description(format);

        if (!format_desc)
                return FALSE;

        if (sample_count > 1)
                return FALSE;

        /* Format wishlist */
        if (format == PIPE_FORMAT_Z24X8_UNORM || format == PIPE_FORMAT_X8Z24_UNORM)
                return FALSE;

        if (format == PIPE_FORMAT_A1B5G5R5_UNORM || format == PIPE_FORMAT_X1B5G5R5_UNORM)
                return FALSE;

        if (bind & PIPE_BIND_RENDER_TARGET) {
                /* TODO: Support all the formats! :) */
                bool supported = util_format_is_rgba8_variant(format_desc);
                supported |= format == PIPE_FORMAT_B5G6R5_UNORM;

                if (!supported)
                        return FALSE;

                if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
                        return FALSE;

                /*
                 * Although possible, it is unnatural to render into compressed or YUV
                 * surfaces. So disable these here to avoid going into weird paths
                 * inside the state trackers.
                 */
                if (format_desc->block.width != 1 ||
                                format_desc->block.height != 1)
                        return FALSE;
        }

        if (bind & PIPE_BIND_DEPTH_STENCIL) {
                if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
                        return FALSE;
        }

        if (format_desc->layout == UTIL_FORMAT_LAYOUT_BPTC ||
                        format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
                        format_desc->layout == UTIL_FORMAT_LAYOUT_ETC) {
                /* Compressed formats not yet hooked up. */
                return FALSE;
        }

        if ((bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
                        ((bind & PIPE_BIND_DISPLAY_TARGET) == 0) &&
                        target != PIPE_BUFFER) {
                const struct util_format_description *desc =
                        util_format_description(format);

                if (desc->nr_channels == 3 && desc->is_array) {
                        /* Don't support any 3-component formats for rendering/texturing
                         * since we don't support the corresponding 8-bit 3 channel UNORM
                         * formats.  This allows us to support GL_ARB_copy_image between
                         * GL_RGB8 and GL_RGB8UI, for example.  Otherwise, we may be asked to
                         * do a resource copy between PIPE_FORMAT_R8G8B8_UINT and
                         * PIPE_FORMAT_R8G8B8X8_UNORM, for example, which will not work
                         * (different bpp).
                         */
                        return FALSE;
                }
        }

        return TRUE;
}


static void
panfrost_destroy_screen( struct pipe_screen *screen )
{
        FREE(screen);
}

static void
panfrost_flush_frontbuffer(struct pipe_screen *_screen,
                           struct pipe_resource *resource,
                           unsigned level, unsigned layer,
                           void *context_private,
                           struct pipe_box *sub_box)
{
        /* TODO: Display target integration */
}

static uint64_t
panfrost_get_timestamp(struct pipe_screen *_screen)
{
        return os_time_get_nano();
}

static void
panfrost_fence_reference(struct pipe_screen *pscreen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        screen->driver->fence_reference(pscreen, ptr, fence);
}

static boolean
panfrost_fence_finish(struct pipe_screen *pscreen,
                      struct pipe_context *ctx,
                      struct pipe_fence_handle *fence,
                      uint64_t timeout)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        return screen->driver->fence_finish(pscreen, ctx, fence, timeout);
}

static const void *
panfrost_screen_get_compiler_options(struct pipe_screen *pscreen,
                                     enum pipe_shader_ir ir,
                                     enum pipe_shader_type shader)
{
        return &midgard_nir_options;
}

struct pipe_screen *
panfrost_create_screen(int fd, struct renderonly *ro)
{
        struct panfrost_screen *screen = CALLOC_STRUCT(panfrost_screen);

	pan_debug = debug_get_option_pan_debug();

        if (!screen)
                return NULL;

        if (ro) {
                screen->ro = renderonly_dup(ro);
                if (!screen->ro) {
                        fprintf(stderr, "Failed to dup renderonly object\n");
                        free(screen);
                        return NULL;
                }
        }

        screen->driver = panfrost_create_drm_driver(fd);

        /* Dump memory and/or performance counters iff asked for in the environment */
        const char *pantrace_base = getenv("PANTRACE_BASE");
        pan_counters_base = getenv("PANCOUNTERS_BASE");

        if (pantrace_base) {
                pantrace_initialize(pantrace_base);
        }

        if (pan_counters_base) {
                screen->driver->allocate_slab(screen, &screen->perf_counters, 64, true, 0, 0, 0);
                screen->driver->enable_counters(screen);
        }

        screen->base.destroy = panfrost_destroy_screen;

        screen->base.get_name = panfrost_get_name;
        screen->base.get_vendor = panfrost_get_vendor;
        screen->base.get_device_vendor = panfrost_get_device_vendor;
        screen->base.get_param = panfrost_get_param;
        screen->base.get_shader_param = panfrost_get_shader_param;
        screen->base.get_paramf = panfrost_get_paramf;
        screen->base.get_timestamp = panfrost_get_timestamp;
        screen->base.is_format_supported = panfrost_is_format_supported;
        screen->base.context_create = panfrost_create_context;
        screen->base.flush_frontbuffer = panfrost_flush_frontbuffer;
        screen->base.get_compiler_options = panfrost_screen_get_compiler_options;
        screen->base.fence_reference = panfrost_fence_reference;
        screen->base.fence_finish = panfrost_fence_finish;

	screen->last_fragment_flushed = true;
        screen->last_job = NULL;

        panfrost_resource_screen_init(screen);

        return &screen->base;
}
