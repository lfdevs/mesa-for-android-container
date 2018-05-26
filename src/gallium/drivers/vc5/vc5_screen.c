/*
 * Copyright © 2014-2017 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#include "os/os_misc.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_hash_table.h"
#include "util/ralloc.h"

#include <xf86drm.h>
#include "vc5_drm.h"
#include "vc5_screen.h"
#include "vc5_context.h"
#include "vc5_resource.h"
#include "compiler/v3d_compiler.h"

static const char *
vc5_screen_get_name(struct pipe_screen *pscreen)
{
        struct vc5_screen *screen = vc5_screen(pscreen);

        if (!screen->name) {
                screen->name = ralloc_asprintf(screen,
                                               "VC5 V3D %d.%d",
                                               screen->devinfo.ver / 10,
                                               screen->devinfo.ver % 10);
        }

        return screen->name;
}

static const char *
vc5_screen_get_vendor(struct pipe_screen *pscreen)
{
        return "Broadcom";
}

static void
vc5_screen_destroy(struct pipe_screen *pscreen)
{
        struct vc5_screen *screen = vc5_screen(pscreen);

        util_hash_table_destroy(screen->bo_handles);
        vc5_bufmgr_destroy(pscreen);
        slab_destroy_parent(&screen->transfer_pool);

        if (using_vc5_simulator)
                vc5_simulator_destroy(screen);

        v3d_compiler_free(screen->compiler);

        close(screen->fd);
        ralloc_free(pscreen);
}

static int
vc5_screen_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
        struct vc5_screen *screen = vc5_screen(pscreen);

        switch (param) {
                /* Supported features (boolean caps). */
        case PIPE_CAP_VERTEX_COLOR_CLAMPED:
        case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
        case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
        case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
        case PIPE_CAP_NPOT_TEXTURES:
        case PIPE_CAP_SHAREABLE_SHADERS:
        case PIPE_CAP_BLEND_EQUATION_SEPARATE:
        case PIPE_CAP_TEXTURE_MULTISAMPLE:
        case PIPE_CAP_TEXTURE_SWIZZLE:
        case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
        case PIPE_CAP_START_INSTANCE:
        case PIPE_CAP_TGSI_INSTANCEID:
        case PIPE_CAP_SM3:
        case PIPE_CAP_TEXTURE_QUERY_LOD:
        case PIPE_CAP_PRIMITIVE_RESTART:
        case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
        case PIPE_CAP_OCCLUSION_QUERY:
        case PIPE_CAP_POINT_SPRITE:
        case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
        case PIPE_CAP_ALLOW_MAPPED_BUFFERS_DURING_EXECUTION:
        case PIPE_CAP_COMPUTE:
        case PIPE_CAP_DRAW_INDIRECT:
        case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
        case PIPE_CAP_SIGNED_VERTEX_BUFFER_OFFSET:
                return 1;

        case PIPE_CAP_INDEP_BLEND_ENABLE:
                return screen->devinfo.ver >= 40;

        case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
                return 256;

        case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
                return 4;

        case PIPE_CAP_GLSL_FEATURE_LEVEL:
                return 400;

        case PIPE_CAP_MAX_VIEWPORTS:
                return 1;

        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
                return 1;
        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
                return 0;
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
                if (screen->devinfo.ver >= 40)
                        return 0;
                else
                        return 1;
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
                if (screen->devinfo.ver >= 40)
                        return 1;
                else
                        return 0;

        case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
        case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
        case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
                return 1;


                /* Stream output. */
        case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
                return 4;
        case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
        case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
                return 64;

        case PIPE_CAP_MIN_TEXEL_OFFSET:
        case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
                return -8;
        case PIPE_CAP_MAX_TEXEL_OFFSET:
        case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
                return 7;

                /* Unsupported features. */
        case PIPE_CAP_ANISOTROPIC_FILTER:
        case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
        case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
        case PIPE_CAP_CUBE_MAP_ARRAY:
        case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
        case PIPE_CAP_SEAMLESS_CUBE_MAP:
        case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
        case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
        case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
        case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
        case PIPE_CAP_SHADER_STENCIL_EXPORT:
        case PIPE_CAP_TGSI_TEXCOORD:
        case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
        case PIPE_CAP_CONDITIONAL_RENDER:
        case PIPE_CAP_TEXTURE_BARRIER:
        case PIPE_CAP_INDEP_BLEND_FUNC:
        case PIPE_CAP_DEPTH_CLIP_DISABLE:
        case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
        case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
        case PIPE_CAP_USER_VERTEX_BUFFERS:
        case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
        case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
        case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
        case PIPE_CAP_TGSI_TES_LAYER_VIEWPORT:
        case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
        case PIPE_CAP_TEXTURE_GATHER_SM5:
        case PIPE_CAP_FAKE_SW_MSAA:
        case PIPE_CAP_SAMPLE_SHADING:
        case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
        case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
        case PIPE_CAP_MAX_VERTEX_STREAMS:
        case PIPE_CAP_MULTI_DRAW_INDIRECT:
        case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
        case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
        case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
        case PIPE_CAP_SAMPLER_VIEW_TARGET:
        case PIPE_CAP_CLIP_HALFZ:
        case PIPE_CAP_VERTEXID_NOBASE:
        case PIPE_CAP_POLYGON_OFFSET_CLAMP:
        case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
        case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
        case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
        case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
        case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
        case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
        case PIPE_CAP_DEPTH_BOUNDS_TEST:
        case PIPE_CAP_TGSI_TXQS:
        case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
        case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
        case PIPE_CAP_CLEAR_TEXTURE:
        case PIPE_CAP_DRAW_PARAMETERS:
        case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
        case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
        case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
        case PIPE_CAP_INVALIDATE_BUFFER:
        case PIPE_CAP_GENERATE_MIPMAP:
        case PIPE_CAP_STRING_MARKER:
        case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
        case PIPE_CAP_QUERY_BUFFER_OBJECT:
        case PIPE_CAP_QUERY_MEMORY_INFO:
        case PIPE_CAP_PCI_GROUP:
        case PIPE_CAP_PCI_BUS:
        case PIPE_CAP_PCI_DEVICE:
        case PIPE_CAP_PCI_FUNCTION:
        case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
        case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
        case PIPE_CAP_CULL_DISTANCE:
        case PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES:
        case PIPE_CAP_TGSI_VOTE:
        case PIPE_CAP_MAX_WINDOW_RECTANGLES:
        case PIPE_CAP_POLYGON_OFFSET_UNITS_UNSCALED:
        case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
        case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
        case PIPE_CAP_TGSI_FS_FBFETCH:
        case PIPE_CAP_INT64:
        case PIPE_CAP_INT64_DIVMOD:
        case PIPE_CAP_DOUBLES:
        case PIPE_CAP_BINDLESS_TEXTURE:
        case PIPE_CAP_POST_DEPTH_COVERAGE:
        case PIPE_CAP_CAN_BIND_CONST_BUFFER_AS_VERTEX:
        case PIPE_CAP_TGSI_BALLOT:
        case PIPE_CAP_SPARSE_BUFFER_PAGE_SIZE:
        case PIPE_CAP_POLYGON_MODE_FILL_RECTANGLE:
        case PIPE_CAP_TGSI_CLOCK:
        case PIPE_CAP_TGSI_TEX_TXF_LZ:
        case PIPE_CAP_NATIVE_FENCE_FD:
        case PIPE_CAP_FENCE_SIGNAL:
        case PIPE_CAP_TGSI_MUL_ZERO_WINS:
        case PIPE_CAP_NIR_SAMPLERS_AS_DEREF:
        case PIPE_CAP_QUERY_SO_OVERFLOW:
        case PIPE_CAP_MEMOBJ:
        case PIPE_CAP_LOAD_CONSTBUF:
        case PIPE_CAP_TILE_RASTER_ORDER:
        case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
        case PIPE_CAP_MAX_COMBINED_SHADER_OUTPUT_RESOURCES:
        case PIPE_CAP_CONTEXT_PRIORITY_MASK:
	case PIPE_CAP_CONSTBUF0_FLAGS:
        case PIPE_CAP_PACKED_UNIFORMS:
                return 0;

                /* Geometry shader output, unsupported. */
        case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
        case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
                return 0;

                /* Texturing. */
        case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
        case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
        case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
                return VC5_MAX_MIP_LEVELS;
        case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
                return 2048;

                /* Render targets. */
        case PIPE_CAP_MAX_RENDER_TARGETS:
                return 4;

                /* Queries. */
        case PIPE_CAP_QUERY_TIME_ELAPSED:
        case PIPE_CAP_QUERY_TIMESTAMP:
                return 0;

        case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
                return 2048;

        case PIPE_CAP_ENDIANNESS:
                return PIPE_ENDIAN_LITTLE;

        case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
                return 64;

        case PIPE_CAP_VENDOR_ID:
                return 0x14E4;
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

        default:
                fprintf(stderr, "unknown param %d\n", param);
                return 0;
        }
}

static float
vc5_screen_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
        switch (param) {
        case PIPE_CAPF_MAX_LINE_WIDTH:
        case PIPE_CAPF_MAX_LINE_WIDTH_AA:
                return 32;

        case PIPE_CAPF_MAX_POINT_WIDTH:
        case PIPE_CAPF_MAX_POINT_WIDTH_AA:
                return 512.0f;

        case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
                return 0.0f;
        case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
                return 16.0f;
        default:
                fprintf(stderr, "unknown paramf %d\n", param);
                return 0;
        }
}

static int
vc5_screen_get_shader_param(struct pipe_screen *pscreen, unsigned shader,
                           enum pipe_shader_cap param)
{
        if (shader != PIPE_SHADER_VERTEX &&
            shader != PIPE_SHADER_FRAGMENT) {
                return 0;
        }

        /* this is probably not totally correct.. but it's a start: */
        switch (param) {
        case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
                return 16384;

        case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
                return UINT_MAX;

        case PIPE_SHADER_CAP_MAX_INPUTS:
                if (shader == PIPE_SHADER_FRAGMENT)
                        return VC5_MAX_FS_INPUTS / 4;
                else
                        return 16;
        case PIPE_SHADER_CAP_MAX_OUTPUTS:
                if (shader == PIPE_SHADER_FRAGMENT)
                        return 4;
                else
                        return VC5_MAX_FS_INPUTS / 4;
        case PIPE_SHADER_CAP_MAX_TEMPS:
                return 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */
        case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
                return 16 * 1024 * sizeof(float);
        case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
                return 16;
        case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
                return 0;
        case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
        case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
        case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
                return 0;
        case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
                return 1;
        case PIPE_SHADER_CAP_SUBROUTINES:
                return 0;
        case PIPE_SHADER_CAP_INTEGERS:
                return 1;
        case PIPE_SHADER_CAP_FP16:
        case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
        case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
        case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
                return 0;
        case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
        case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
        case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
        case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
                return VC5_MAX_TEXTURE_SAMPLERS;
        case PIPE_SHADER_CAP_PREFERRED_IR:
                return PIPE_SHADER_IR_NIR;
        case PIPE_SHADER_CAP_SUPPORTED_IRS:
                return 0;
        case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
                return 32;
        case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
        case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
                return 0;
        default:
                fprintf(stderr, "unknown shader param %d\n", param);
                return 0;
        }
        return 0;
}

static boolean
vc5_screen_is_format_supported(struct pipe_screen *pscreen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned usage)
{
        struct vc5_screen *screen = vc5_screen(pscreen);

        if (sample_count > 1 && sample_count != VC5_MAX_SAMPLES)
                return FALSE;

        if ((target >= PIPE_MAX_TEXTURE_TYPES) ||
            !util_format_is_supported(format, usage)) {
                return FALSE;
        }

        if (usage & PIPE_BIND_VERTEX_BUFFER) {
                switch (format) {
                case PIPE_FORMAT_R32G32B32A32_FLOAT:
                case PIPE_FORMAT_R32G32B32_FLOAT:
                case PIPE_FORMAT_R32G32_FLOAT:
                case PIPE_FORMAT_R32_FLOAT:
                case PIPE_FORMAT_R32G32B32A32_SNORM:
                case PIPE_FORMAT_R32G32B32_SNORM:
                case PIPE_FORMAT_R32G32_SNORM:
                case PIPE_FORMAT_R32_SNORM:
                case PIPE_FORMAT_R32G32B32A32_SSCALED:
                case PIPE_FORMAT_R32G32B32_SSCALED:
                case PIPE_FORMAT_R32G32_SSCALED:
                case PIPE_FORMAT_R32_SSCALED:
                case PIPE_FORMAT_R16G16B16A16_UNORM:
                case PIPE_FORMAT_R16G16B16_UNORM:
                case PIPE_FORMAT_R16G16_UNORM:
                case PIPE_FORMAT_R16_UNORM:
                case PIPE_FORMAT_R16G16B16A16_SNORM:
                case PIPE_FORMAT_R16G16B16_SNORM:
                case PIPE_FORMAT_R16G16_SNORM:
                case PIPE_FORMAT_R16_SNORM:
                case PIPE_FORMAT_R16G16B16A16_USCALED:
                case PIPE_FORMAT_R16G16B16_USCALED:
                case PIPE_FORMAT_R16G16_USCALED:
                case PIPE_FORMAT_R16_USCALED:
                case PIPE_FORMAT_R16G16B16A16_SSCALED:
                case PIPE_FORMAT_R16G16B16_SSCALED:
                case PIPE_FORMAT_R16G16_SSCALED:
                case PIPE_FORMAT_R16_SSCALED:
                case PIPE_FORMAT_R8G8B8A8_UNORM:
                case PIPE_FORMAT_R8G8B8_UNORM:
                case PIPE_FORMAT_R8G8_UNORM:
                case PIPE_FORMAT_R8_UNORM:
                case PIPE_FORMAT_R8G8B8A8_SNORM:
                case PIPE_FORMAT_R8G8B8_SNORM:
                case PIPE_FORMAT_R8G8_SNORM:
                case PIPE_FORMAT_R8_SNORM:
                case PIPE_FORMAT_R8G8B8A8_USCALED:
                case PIPE_FORMAT_R8G8B8_USCALED:
                case PIPE_FORMAT_R8G8_USCALED:
                case PIPE_FORMAT_R8_USCALED:
                case PIPE_FORMAT_R8G8B8A8_SSCALED:
                case PIPE_FORMAT_R8G8B8_SSCALED:
                case PIPE_FORMAT_R8G8_SSCALED:
                case PIPE_FORMAT_R8_SSCALED:
                case PIPE_FORMAT_R10G10B10A2_UNORM:
                case PIPE_FORMAT_B10G10R10A2_UNORM:
                case PIPE_FORMAT_R10G10B10A2_SNORM:
                case PIPE_FORMAT_B10G10R10A2_SNORM:
                case PIPE_FORMAT_R10G10B10A2_USCALED:
                case PIPE_FORMAT_B10G10R10A2_USCALED:
                case PIPE_FORMAT_R10G10B10A2_SSCALED:
                case PIPE_FORMAT_B10G10R10A2_SSCALED:
                        break;
                default:
                        return FALSE;
                }
        }

        if ((usage & PIPE_BIND_RENDER_TARGET) &&
            !vc5_rt_format_supported(&screen->devinfo, format)) {
                return FALSE;
        }

        if ((usage & PIPE_BIND_SAMPLER_VIEW) &&
            !vc5_tex_format_supported(&screen->devinfo, format)) {
                return FALSE;
        }

        if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
            !(format == PIPE_FORMAT_S8_UINT_Z24_UNORM ||
              format == PIPE_FORMAT_X8Z24_UNORM ||
              format == PIPE_FORMAT_Z16_UNORM ||
              format == PIPE_FORMAT_Z32_FLOAT ||
              format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) {
                return FALSE;
        }

        if ((usage & PIPE_BIND_INDEX_BUFFER) &&
            !(format == PIPE_FORMAT_I8_UINT ||
              format == PIPE_FORMAT_I16_UINT ||
              format == PIPE_FORMAT_I32_UINT)) {
                return FALSE;
        }

        return TRUE;
}

#define PTR_TO_UINT(x) ((unsigned)((intptr_t)(x)))

static unsigned handle_hash(void *key)
{
    return PTR_TO_UINT(key);
}

static int handle_compare(void *key1, void *key2)
{
    return PTR_TO_UINT(key1) != PTR_TO_UINT(key2);
}

static bool
vc5_get_device_info(struct vc5_screen *screen)
{
        struct drm_vc5_get_param ident0 = {
                .param = DRM_VC5_PARAM_V3D_CORE0_IDENT0,
        };
        struct drm_vc5_get_param ident1 = {
                .param = DRM_VC5_PARAM_V3D_CORE0_IDENT1,
        };
        int ret;

        ret = vc5_ioctl(screen->fd, DRM_IOCTL_VC5_GET_PARAM, &ident0);
        if (ret != 0) {
                fprintf(stderr, "Couldn't get V3D core IDENT0: %s\n",
                        strerror(errno));
                return false;
        }
        ret = vc5_ioctl(screen->fd, DRM_IOCTL_VC5_GET_PARAM, &ident1);
        if (ret != 0) {
                fprintf(stderr, "Couldn't get V3D core IDENT1: %s\n",
                        strerror(errno));
                return false;
        }

        uint32_t major = (ident0.value >> 24) & 0xff;
        uint32_t minor = (ident1.value >> 0) & 0xf;
        screen->devinfo.ver = major * 10 + minor;

        switch (screen->devinfo.ver) {
        case 33:
        case 41:
        case 42:
                break;
        default:
                fprintf(stderr,
                        "V3D %d.%d not supported by this version of Mesa.\n",
                        screen->devinfo.ver / 10,
                        screen->devinfo.ver % 10);
                return false;
        }

        return true;
}

static const void *
vc5_screen_get_compiler_options(struct pipe_screen *pscreen,
                                enum pipe_shader_ir ir, unsigned shader)
{
        return &v3d_nir_options;
}

struct pipe_screen *
vc5_screen_create(int fd)
{
        struct vc5_screen *screen = rzalloc(NULL, struct vc5_screen);
        struct pipe_screen *pscreen;

        pscreen = &screen->base;

        pscreen->destroy = vc5_screen_destroy;
        pscreen->get_param = vc5_screen_get_param;
        pscreen->get_paramf = vc5_screen_get_paramf;
        pscreen->get_shader_param = vc5_screen_get_shader_param;
        pscreen->context_create = vc5_context_create;
        pscreen->is_format_supported = vc5_screen_is_format_supported;

        screen->fd = fd;
        list_inithead(&screen->bo_cache.time_list);
        (void)mtx_init(&screen->bo_handles_mutex, mtx_plain);
        screen->bo_handles = util_hash_table_create(handle_hash, handle_compare);

#if defined(USE_VC5_SIMULATOR)
        vc5_simulator_init(screen);
#endif

        if (!vc5_get_device_info(screen))
                goto fail;

        slab_create_parent(&screen->transfer_pool, sizeof(struct vc5_transfer), 16);

        vc5_fence_init(screen);

        v3d_process_debug_variable();

        vc5_resource_screen_init(pscreen);

        screen->compiler = v3d_compiler_init(&screen->devinfo);

        pscreen->get_name = vc5_screen_get_name;
        pscreen->get_vendor = vc5_screen_get_vendor;
        pscreen->get_device_vendor = vc5_screen_get_vendor;
        pscreen->get_compiler_options = vc5_screen_get_compiler_options;

        return pscreen;

fail:
        close(fd);
        ralloc_free(pscreen);
        return NULL;
}
