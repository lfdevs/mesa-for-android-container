/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_format_s3tc.h"
#include "util/u_video.h"
#include "util/u_screen.h"
#include "util/os_time.h"
#include "util/u_process.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"

#include <fcntl.h>

#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/panfrost_drm.h"

#include "pan_bo.h"
#include "pan_screen.h"
#include "pan_resource.h"
#include "pan_public.h"
#include "pan_util.h"
#include "pandecode/decode.h"

#include "pan_context.h"
#include "midgard/midgard_compile.h"

static const struct debug_named_value debug_options[] = {
        {"msgs",      PAN_DBG_MSGS,	"Print debug messages"},
        {"trace",     PAN_DBG_TRACE,    "Trace the command stream"},
        {"deqp",      PAN_DBG_DEQP,     "Hacks for dEQP"},
        {"afbc",      PAN_DBG_AFBC,     "Enable non-conformant AFBC impl"},
        DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(pan_debug, "PAN_MESA_DEBUG", debug_options, 0)

int pan_debug = 0;

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
        /* We expose in-dev stuff for dEQP that we don't want apps to use yet */
        bool is_deqp = pan_debug & PAN_DBG_DEQP;

        switch (param) {
        case PIPE_CAP_NPOT_TEXTURES:
        case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
        case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
        case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
        case PIPE_CAP_VERTEX_SHADER_SATURATE:
        case PIPE_CAP_POINT_SPRITE:
                return 1;

        case PIPE_CAP_MAX_RENDER_TARGETS:
                return is_deqp ? 4 : 1;

        /* Throttling frames breaks pipelining */
        case PIPE_CAP_THROTTLE:
                return 0;

        case PIPE_CAP_OCCLUSION_QUERY:
                return 1;
        case PIPE_CAP_QUERY_TIME_ELAPSED:
        case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
        case PIPE_CAP_QUERY_TIMESTAMP:
        case PIPE_CAP_QUERY_SO_OVERFLOW:
                return 0;

        case PIPE_CAP_TEXTURE_SWIZZLE:
                return 1;

        case PIPE_CAP_TGSI_INSTANCEID:
        case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
                return is_deqp ? 1 : 0;

        case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
                return is_deqp ? 4 : 0;
        case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
        case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
                return is_deqp ? 64 : 0;
        case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
                return is_deqp ? 256 : 0; /* for GL3 */

        case PIPE_CAP_GLSL_FEATURE_LEVEL:
        case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
                return is_deqp ? 140 : 120;
        case PIPE_CAP_ESSL_FEATURE_LEVEL:
                return is_deqp ? 300 : 120;

        case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
                return is_deqp ? 16 : 0;

        case PIPE_CAP_CUBE_MAP_ARRAY:
                return is_deqp;

        /* For faking GLES 3.1 for dEQP-GLES31 */
        case PIPE_CAP_TEXTURE_MULTISAMPLE:
        case PIPE_CAP_MAX_COMBINED_HW_ATOMIC_COUNTERS:
        case PIPE_CAP_MAX_COMBINED_HW_ATOMIC_COUNTER_BUFFERS:
        case PIPE_CAP_IMAGE_LOAD_FORMATTED:
                return is_deqp;

        /* For faking compute shaders */
        case PIPE_CAP_COMPUTE:
                return is_deqp;

        /* TODO: Where does this req come from in practice? */
        case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
                return 4096;
        case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
        case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
                return 13;

        case PIPE_CAP_BLEND_EQUATION_SEPARATE:
        case PIPE_CAP_INDEP_BLEND_ENABLE:
        case PIPE_CAP_INDEP_BLEND_FUNC:
                return 1;

        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
                /* Hardware is natively upper left */
                return 0;

        case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
        case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
        case PIPE_CAP_GENERATE_MIPMAP:
                return 1;

        /* We would prefer varyings */
        case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
        case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
                return 0;

        /* I really don't want to set this CAP but let's not swim against the
         * tide.. */
        case PIPE_CAP_TGSI_TEXCOORD:
                return 1;

        case PIPE_CAP_SEAMLESS_CUBE_MAP:
        case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
                return 1;

        case PIPE_CAP_MAX_VERTEX_ELEMENT_SRC_OFFSET:
                return 0xffff;

        case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
                return 1;

        case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
                return 65536;

        case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
                return 0;

        case PIPE_CAP_ENDIANNESS:
                return PIPE_ENDIAN_NATIVE;

        case PIPE_CAP_SAMPLER_VIEW_TARGET:
                return 1;

        case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
                return -8;

        case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
                return 7;

        case PIPE_CAP_VENDOR_ID:
        case PIPE_CAP_DEVICE_ID:
                return 0xFFFFFFFF;

        case PIPE_CAP_ACCELERATED:
        case PIPE_CAP_UMA:
        case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
        case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
        case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
        case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
                return 1;

        case PIPE_CAP_VIDEO_MEMORY: {
                uint64_t system_memory;

                if (!os_get_total_physical_memory(&system_memory))
                        return 0;

                return (int)(system_memory >> 20);
        }

        case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
                return 4;

        case PIPE_CAP_MAX_VARYINGS:
                return 16;

        case PIPE_CAP_ALPHA_TEST:
                return 0;

        default:
                return u_pipe_screen_get_param_defaults(screen, param);
        }
}

static int
panfrost_get_shader_param(struct pipe_screen *screen,
                          enum pipe_shader_type shader,
                          enum pipe_shader_cap param)
{
        bool is_deqp = pan_debug & PAN_DBG_DEQP;

        if (shader != PIPE_SHADER_VERTEX &&
            shader != PIPE_SHADER_FRAGMENT &&
            !(shader == PIPE_SHADER_COMPUTE && is_deqp))
                return 0;

        /* this is probably not totally correct.. but it's a start: */
        switch (param) {
        case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
        case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
                return 16384;

        case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
                return 1024;

        case PIPE_SHADER_CAP_MAX_INPUTS:
                return 16;

        case PIPE_SHADER_CAP_MAX_OUTPUTS:
                return shader == PIPE_SHADER_FRAGMENT ? 4 : 8;

        case PIPE_SHADER_CAP_MAX_TEMPS:
                return 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */

        case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
                return 16 * 1024 * sizeof(float);

        case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
                return PAN_MAX_CONST_BUFFERS;

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
                return (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_NIR_SERIALIZED);

        case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
                return 32;

        case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
        case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
                return is_deqp ? 4 : 0;
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
        case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
                return 0;

        case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
        case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
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
                return 1024.0;

        case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
                return 16.0;

        case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
                return 16.0; /* arbitrary */

        case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
        case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
        case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
                return 0.0f;

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
static bool
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
                return false;

        if (sample_count > 1)
                return false;

        /* Format wishlist */
        if (format == PIPE_FORMAT_X8Z24_UNORM)
                return false;

        if (format == PIPE_FORMAT_A1B5G5R5_UNORM || format == PIPE_FORMAT_X1B5G5R5_UNORM)
                return false;

        /* TODO */
        if (format == PIPE_FORMAT_B5G5R5A1_UNORM)
                return FALSE;

        /* Don't confuse poorly written apps (workaround dEQP bug) that expect
         * more alpha than they ask for */

        bool scanout = bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED | PIPE_BIND_DISPLAY_TARGET);
        bool renderable = bind & PIPE_BIND_RENDER_TARGET;

        if (scanout && renderable && !util_format_is_rgba8_variant(format_desc))
                return false;

        if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN &&
            format_desc->layout != UTIL_FORMAT_LAYOUT_OTHER) {
                /* Compressed formats not yet hooked up. */
                return false;
        }

        /* Internally, formats that are depth/stencil renderable are limited.
         *
         * In particular: Z16, Z24, Z24S8, S8 are all identical from the GPU
         * rendering perspective. That is, we render to Z24S8 (which we can
         * AFBC compress), ignore the different when texturing (who cares?),
         * and then in the off-chance there's a CPU read we blit back to
         * staging.
         *
         * ...alternatively, we can make the state tracker deal with that. */

        if (bind & PIPE_BIND_DEPTH_STENCIL) {
                switch (format) {
                        case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                        case PIPE_FORMAT_Z24X8_UNORM:
                        case PIPE_FORMAT_Z32_UNORM:
                        case PIPE_FORMAT_Z32_FLOAT:
                        case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                                return true;

                        default:
                                return false;
                }
        }

        return true;
}

static int
panfrost_get_compute_param(struct pipe_screen *pscreen, enum pipe_shader_ir ir_type,
                enum pipe_compute_cap param, void *ret)
{
	const char * const ir = "panfrost";

	if (!(pan_debug & PAN_DBG_DEQP))
		return 0;

#define RET(x) do {                  \
   if (ret)                          \
      memcpy(ret, x, sizeof(x));     \
   return sizeof(x);                 \
} while (0)

	switch (param) {
	case PIPE_COMPUTE_CAP_ADDRESS_BITS:
                /* TODO: We'll want 64-bit pointers soon */
		RET((uint32_t []){ 32 });

	case PIPE_COMPUTE_CAP_IR_TARGET:
		if (ret)
			sprintf(ret, "%s", ir);
		return strlen(ir) * sizeof(char);

	case PIPE_COMPUTE_CAP_GRID_DIMENSION:
		RET((uint64_t []) { 3 });

	case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
		RET(((uint64_t []) { 65535, 65535, 65535 }));

	case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
		RET(((uint64_t []) { 1024, 1024, 64 }));

	case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
		RET((uint64_t []) { 1024 });

	case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
		RET((uint64_t []) { 1024*1024*512 /* Maybe get memory */ });

	case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
		RET((uint64_t []) { 32768 });

	case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE:
	case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
		RET((uint64_t []) { 4096 });

	case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE:
		RET((uint64_t []) { 1024*1024*512 /* Maybe get memory */ });

	case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
		RET((uint32_t []) { 800 /* MHz -- TODO */ });

	case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
		RET((uint32_t []) { 9999 });  // TODO

	case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
		RET((uint32_t []) { 1 }); // TODO

	case PIPE_COMPUTE_CAP_SUBGROUP_SIZE:
		RET((uint32_t []) { 32 });  // TODO

	case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
		RET((uint64_t []) { 1024 }); // TODO
	}

	return 0;
}

static void
panfrost_destroy_screen(struct pipe_screen *pscreen)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        panfrost_bo_cache_evict_all(screen);
        pthread_mutex_destroy(&screen->bo_cache_lock);
        pthread_mutex_destroy(&screen->active_bos_lock);
        drmFreeVersion(screen->kernel_version);
        ralloc_free(screen);
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
        struct panfrost_fence **p = (struct panfrost_fence **)ptr;
        struct panfrost_fence *f = (struct panfrost_fence *)fence;
        struct panfrost_fence *old = *p;

        if (pipe_reference(&(*p)->reference, &f->reference)) {
                util_dynarray_foreach(&old->syncfds, int, fd)
                        close(*fd);
                util_dynarray_fini(&old->syncfds);
                free(old);
        }
        *p = f;
}

static bool
panfrost_fence_finish(struct pipe_screen *pscreen,
                      struct pipe_context *ctx,
                      struct pipe_fence_handle *fence,
                      uint64_t timeout)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_fence *f = (struct panfrost_fence *)fence;
        struct util_dynarray syncobjs;
        int ret;

        /* All fences were already signaled */
        if (!util_dynarray_num_elements(&f->syncfds, int))
                return true;

        util_dynarray_init(&syncobjs, NULL);
        util_dynarray_foreach(&f->syncfds, int, fd) {
                uint32_t syncobj;

                ret = drmSyncobjCreate(screen->fd, 0, &syncobj);
                assert(!ret);

                ret = drmSyncobjImportSyncFile(screen->fd, syncobj, *fd);
                assert(!ret);
                util_dynarray_append(&syncobjs, uint32_t, syncobj);
        }

        uint64_t abs_timeout = os_time_get_absolute_timeout(timeout);
        if (abs_timeout == OS_TIMEOUT_INFINITE)
                abs_timeout = INT64_MAX;

        ret = drmSyncobjWait(screen->fd, util_dynarray_begin(&syncobjs),
                             util_dynarray_num_elements(&syncobjs, uint32_t),
                             abs_timeout, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                             NULL);

        util_dynarray_foreach(&syncobjs, uint32_t, syncobj)
                drmSyncobjDestroy(screen->fd, *syncobj);

        return ret >= 0;
}

struct panfrost_fence *
panfrost_fence_create(struct panfrost_context *ctx,
                      struct util_dynarray *fences)
{
        struct panfrost_screen *screen = pan_screen(ctx->base.screen);
        struct panfrost_fence *f = calloc(1, sizeof(*f));
        if (!f)
                return NULL;

        util_dynarray_init(&f->syncfds, NULL);

        /* Export fences from all pending batches. */
        util_dynarray_foreach(fences, struct panfrost_batch_fence *, fence) {
                int fd = -1;

                /* The fence is already signaled, no need to export it. */
                if ((*fence)->signaled)
                        continue;

                drmSyncobjExportSyncFile(screen->fd, (*fence)->syncobj, &fd);
                if (fd == -1)
                        fprintf(stderr, "export failed: %m\n");

                assert(fd != -1);
                util_dynarray_append(&f->syncfds, int, fd);
        }

        pipe_reference_init(&f->reference, 1);

        return f;
}

static const void *
panfrost_screen_get_compiler_options(struct pipe_screen *pscreen,
                                     enum pipe_shader_ir ir,
                                     enum pipe_shader_type shader)
{
        return &midgard_nir_options;
}

static unsigned
panfrost_query_gpu_version(struct panfrost_screen *screen)
{
        struct drm_panfrost_get_param get_param = {0,};
        ASSERTED int ret;

        get_param.param = DRM_PANFROST_PARAM_GPU_PROD_ID;
        ret = drmIoctl(screen->fd, DRM_IOCTL_PANFROST_GET_PARAM, &get_param);
        assert(!ret);

        return get_param.value;
}

static uint32_t
panfrost_active_bos_hash(const void *key)
{
        const struct panfrost_bo *bo = key;

        return _mesa_hash_data(&bo->gem_handle, sizeof(bo->gem_handle));
}

static bool
panfrost_active_bos_cmp(const void *keya, const void *keyb)
{
        const struct panfrost_bo *a = keya, *b = keyb;

        return a->gem_handle == b->gem_handle;
}

struct pipe_screen *
panfrost_create_screen(int fd, struct renderonly *ro)
{
        pan_debug = debug_get_option_pan_debug();

        /* Blacklist apps known to be buggy under Panfrost */
        const char *proc = util_get_process_name();
        const char *blacklist[] = {
                "chromium",
                "chrome",
        };

        for (unsigned i = 0; i < ARRAY_SIZE(blacklist); ++i) {
                if ((strcmp(blacklist[i], proc) == 0))
                        return NULL;
        }

        /* Create the screen */
        struct panfrost_screen *screen = rzalloc(NULL, struct panfrost_screen);

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

        screen->fd = fd;

        screen->gpu_id = panfrost_query_gpu_version(screen);
        screen->require_sfbd = screen->gpu_id < 0x0750; /* T760 is the first to support MFBD */
        screen->kernel_version = drmGetVersion(fd);

        /* Check if we're loading against a supported GPU model. */

        switch (screen->gpu_id) {
        case 0x750: /* T760 */
        case 0x820: /* T820 */
        case 0x860: /* T860 */
                break;
        default:
                /* Fail to load against untested models */
                debug_printf("panfrost: Unsupported model %X",
                             screen->gpu_id);
                return NULL;
        }

        pthread_mutex_init(&screen->active_bos_lock, NULL);
        screen->active_bos = _mesa_set_create(screen, panfrost_active_bos_hash,
                                              panfrost_active_bos_cmp);

        pthread_mutex_init(&screen->bo_cache_lock, NULL);
        for (unsigned i = 0; i < ARRAY_SIZE(screen->bo_cache); ++i)
                list_inithead(&screen->bo_cache[i]);

        if (pan_debug & PAN_DBG_TRACE)
                pandecode_initialize();

        screen->base.destroy = panfrost_destroy_screen;

        screen->base.get_name = panfrost_get_name;
        screen->base.get_vendor = panfrost_get_vendor;
        screen->base.get_device_vendor = panfrost_get_device_vendor;
        screen->base.get_param = panfrost_get_param;
        screen->base.get_shader_param = panfrost_get_shader_param;
        screen->base.get_compute_param = panfrost_get_compute_param;
        screen->base.get_paramf = panfrost_get_paramf;
        screen->base.get_timestamp = panfrost_get_timestamp;
        screen->base.is_format_supported = panfrost_is_format_supported;
        screen->base.context_create = panfrost_create_context;
        screen->base.flush_frontbuffer = panfrost_flush_frontbuffer;
        screen->base.get_compiler_options = panfrost_screen_get_compiler_options;
        screen->base.fence_reference = panfrost_fence_reference;
        screen->base.fence_finish = panfrost_fence_finish;
        screen->base.set_damage_region = panfrost_resource_set_damage_region;

        panfrost_resource_screen_init(screen);

        return &screen->base;
}
