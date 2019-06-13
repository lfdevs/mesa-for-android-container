/*
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * Copyright (C) 2017-2019 Connor Abbott
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
 */

#include <panfrost-job.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <stdarg.h>
#include "mmap.h"

#include "../pan_pretty_print.h"
#include "../midgard/disassemble.h"
int pandecode_replay_jc(mali_ptr jc_gpu_va, bool bifrost);

#define MEMORY_PROP(obj, p) {\
	char *a = pointer_as_memory_reference(obj->p); \
	pandecode_prop("%s = %s", #p, a); \
	free(a); \
}

#define MEMORY_COMMENT(obj, p) {\
	char *a = pointer_as_memory_reference(obj->p); \
	pandecode_msg("%s = %s\n", #p, a); \
	free(a); \
}

#define DYN_MEMORY_PROP(obj, no, p) { \
	if (obj->p) \
		pandecode_prop("%s = %s_%d_p", #p, #p, no); \
}

/* Semantic logging type.
 *
 * Raw: for raw messages to be printed as is.
 * Message: for helpful information to be commented out in replays.
 * Property: for properties of a struct
 *
 * Use one of pandecode_log, pandecode_msg, or pandecode_prop as syntax sugar.
 */

enum pandecode_log_type {
        PANDECODE_RAW,
        PANDECODE_MESSAGE,
        PANDECODE_PROPERTY
};

#define pandecode_log(...)  pandecode_log_typed(PANDECODE_RAW,      __VA_ARGS__)
#define pandecode_msg(...)  pandecode_log_typed(PANDECODE_MESSAGE,  __VA_ARGS__)
#define pandecode_prop(...) pandecode_log_typed(PANDECODE_PROPERTY, __VA_ARGS__)

unsigned pandecode_indent = 0;

static void
pandecode_make_indent(void)
{
        for (unsigned i = 0; i < pandecode_indent; ++i)
                printf("    ");
}

static void
pandecode_log_typed(enum pandecode_log_type type, const char *format, ...)
{
        va_list ap;

        pandecode_make_indent();

        if (type == PANDECODE_MESSAGE)
                printf("// ");
        else if (type == PANDECODE_PROPERTY)
                printf(".");

        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);

        if (type == PANDECODE_PROPERTY)
                printf(",\n");
}

static void
pandecode_log_cont(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);
}

struct pandecode_flag_info {
        u64 flag;
        const char *name;
};

static void
pandecode_log_decoded_flags(const struct pandecode_flag_info *flag_info,
                          u64 flags)
{
        bool decodable_flags_found = false;

        for (int i = 0; flag_info[i].name; i++) {
                if ((flags & flag_info[i].flag) != flag_info[i].flag)
                        continue;

                if (!decodable_flags_found) {
                        decodable_flags_found = true;
                } else {
                        pandecode_log_cont(" | ");
                }

                pandecode_log_cont("%s", flag_info[i].name);

                flags &= ~flag_info[i].flag;
        }

        if (decodable_flags_found) {
                if (flags)
                        pandecode_log_cont(" | 0x%" PRIx64, flags);
        } else {
                pandecode_log_cont("0x%" PRIx64, flags);
        }
}

#define FLAG_INFO(flag) { MALI_##flag, "MALI_" #flag }
static const struct pandecode_flag_info gl_enable_flag_info[] = {
        FLAG_INFO(CULL_FACE_FRONT),
        FLAG_INFO(CULL_FACE_BACK),
        FLAG_INFO(OCCLUSION_QUERY),
        FLAG_INFO(OCCLUSION_PRECISE),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_CLEAR_##flag, "MALI_CLEAR_" #flag }
static const struct pandecode_flag_info clear_flag_info[] = {
        FLAG_INFO(FAST),
        FLAG_INFO(SLOW),
        FLAG_INFO(SLOW_STENCIL),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_MASK_##flag, "MALI_MASK_" #flag }
static const struct pandecode_flag_info mask_flag_info[] = {
        FLAG_INFO(R),
        FLAG_INFO(G),
        FLAG_INFO(B),
        FLAG_INFO(A),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_##flag, "MALI_" #flag }
static const struct pandecode_flag_info u3_flag_info[] = {
        FLAG_INFO(HAS_MSAA),
        FLAG_INFO(CAN_DISCARD),
        FLAG_INFO(HAS_BLEND_SHADER),
        FLAG_INFO(DEPTH_TEST),
        {}
};

static const struct pandecode_flag_info u4_flag_info[] = {
        FLAG_INFO(NO_MSAA),
        FLAG_INFO(NO_DITHER),
        FLAG_INFO(DEPTH_RANGE_A),
        FLAG_INFO(DEPTH_RANGE_B),
        FLAG_INFO(STENCIL_TEST),
        FLAG_INFO(SAMPLE_ALPHA_TO_COVERAGE_NO_BLEND_SHADER),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_FRAMEBUFFER_##flag, "MALI_FRAMEBUFFER_" #flag }
static const struct pandecode_flag_info fb_fmt_flag_info[] = {
        FLAG_INFO(MSAA_A),
        FLAG_INFO(MSAA_B),
        FLAG_INFO(MSAA_8),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_MFBD_FORMAT_##flag, "MALI_MFBD_FORMAT_" #flag }
static const struct pandecode_flag_info mfbd_fmt_flag_info[] = {
        FLAG_INFO(AFBC),
        FLAG_INFO(MSAA),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_EXTRA_##flag, "MALI_EXTRA_" #flag }
static const struct pandecode_flag_info mfbd_extra_flag_info[] = {
        FLAG_INFO(PRESENT),
        FLAG_INFO(AFBC),
        FLAG_INFO(ZS),
        {}
};
#undef FLAG_INFO

#define FLAG_INFO(flag) { MALI_##flag, "MALI_" #flag }
static const struct pandecode_flag_info shader_unknown1_flag_info [] = {
        FLAG_INFO(NO_ALPHA_TO_COVERAGE),
        FLAG_INFO(READS_TILEBUFFER),
        FLAG_INFO(READS_ZS),
        {}
};
#undef FLAG_INFO

extern char *replace_fragment;
extern char *replace_vertex;

static char *
pandecode_job_type_name(enum mali_job_type type)
{
#define DEFINE_CASE(name) case JOB_TYPE_ ## name: return "JOB_TYPE_" #name

        switch (type) {
                DEFINE_CASE(NULL);
                DEFINE_CASE(SET_VALUE);
                DEFINE_CASE(CACHE_FLUSH);
                DEFINE_CASE(COMPUTE);
                DEFINE_CASE(VERTEX);
                DEFINE_CASE(TILER);
                DEFINE_CASE(FUSED);
                DEFINE_CASE(FRAGMENT);

        case JOB_NOT_STARTED:
                return "NOT_STARTED";

        default:
                pandecode_log("Warning! Unknown job type %x\n", type);
                return "!?!?!?";
        }

#undef DEFINE_CASE
}

static char *
pandecode_draw_mode_name(enum mali_draw_mode mode)
{
#define DEFINE_CASE(name) case MALI_ ## name: return "MALI_" #name

        switch (mode) {
                DEFINE_CASE(DRAW_NONE);
                DEFINE_CASE(POINTS);
                DEFINE_CASE(LINES);
                DEFINE_CASE(TRIANGLES);
                DEFINE_CASE(TRIANGLE_STRIP);
                DEFINE_CASE(TRIANGLE_FAN);
                DEFINE_CASE(LINE_STRIP);
                DEFINE_CASE(LINE_LOOP);
                DEFINE_CASE(POLYGON);
                DEFINE_CASE(QUADS);
                DEFINE_CASE(QUAD_STRIP);

        default:
                return "MALI_TRIANGLES /* XXX: Unknown GL mode, check dump */";
        }

#undef DEFINE_CASE
}

#define DEFINE_CASE(name) case MALI_FUNC_ ## name: return "MALI_FUNC_" #name
static char *
pandecode_func_name(enum mali_func mode)
{
        switch (mode) {
                DEFINE_CASE(NEVER);
                DEFINE_CASE(LESS);
                DEFINE_CASE(EQUAL);
                DEFINE_CASE(LEQUAL);
                DEFINE_CASE(GREATER);
                DEFINE_CASE(NOTEQUAL);
                DEFINE_CASE(GEQUAL);
                DEFINE_CASE(ALWAYS);

        default:
                return "MALI_FUNC_NEVER /* XXX: Unknown function, check dump */";
        }
}
#undef DEFINE_CASE

/* Why is this duplicated? Who knows... */
#define DEFINE_CASE(name) case MALI_ALT_FUNC_ ## name: return "MALI_ALT_FUNC_" #name
static char *
pandecode_alt_func_name(enum mali_alt_func mode)
{
        switch (mode) {
                DEFINE_CASE(NEVER);
                DEFINE_CASE(LESS);
                DEFINE_CASE(EQUAL);
                DEFINE_CASE(LEQUAL);
                DEFINE_CASE(GREATER);
                DEFINE_CASE(NOTEQUAL);
                DEFINE_CASE(GEQUAL);
                DEFINE_CASE(ALWAYS);

        default:
                return "MALI_FUNC_NEVER /* XXX: Unknown function, check dump */";
        }
}
#undef DEFINE_CASE

#define DEFINE_CASE(name) case MALI_STENCIL_ ## name: return "MALI_STENCIL_" #name
static char *
pandecode_stencil_op_name(enum mali_stencil_op op)
{
        switch (op) {
                DEFINE_CASE(KEEP);
                DEFINE_CASE(REPLACE);
                DEFINE_CASE(ZERO);
                DEFINE_CASE(INVERT);
                DEFINE_CASE(INCR_WRAP);
                DEFINE_CASE(DECR_WRAP);
                DEFINE_CASE(INCR);
                DEFINE_CASE(DECR);

        default:
                return "MALI_STENCIL_KEEP /* XXX: Unknown stencil op, check dump */";
        }
}

#undef DEFINE_CASE

#define DEFINE_CASE(name) case MALI_ATTR_ ## name: return "MALI_ATTR_" #name
static char *pandecode_attr_mode_name(enum mali_attr_mode mode)
{
	switch(mode) {
	DEFINE_CASE(UNUSED);
	DEFINE_CASE(LINEAR);
	DEFINE_CASE(POT_DIVIDE);
	DEFINE_CASE(MODULO);
	DEFINE_CASE(NPOT_DIVIDE);
	default: return "MALI_ATTR_UNUSED /* XXX: Unknown stencil op, check dump */";
	}
}

#undef DEFINE_CASE

#define DEFINE_CASE(name) case MALI_CHANNEL_## name: return "MALI_CHANNEL_" #name
static char *
pandecode_channel_name(enum mali_channel channel)
{
        switch (channel) {
                DEFINE_CASE(RED);
                DEFINE_CASE(GREEN);
                DEFINE_CASE(BLUE);
                DEFINE_CASE(ALPHA);
                DEFINE_CASE(ZERO);
                DEFINE_CASE(ONE);
                DEFINE_CASE(RESERVED_0);
                DEFINE_CASE(RESERVED_1);

        default:
                return "MALI_CHANNEL_ZERO /* XXX: Unknown channel, check dump */";
        }
}
#undef DEFINE_CASE

#define DEFINE_CASE(name) case MALI_WRAP_## name: return "MALI_WRAP_" #name
static char *
pandecode_wrap_mode_name(enum mali_wrap_mode op)
{
        switch (op) {
                DEFINE_CASE(REPEAT);
                DEFINE_CASE(CLAMP_TO_EDGE);
                DEFINE_CASE(CLAMP_TO_BORDER);
                DEFINE_CASE(MIRRORED_REPEAT);

        default:
                return "MALI_WRAP_REPEAT /* XXX: Unknown wrap mode, check dump */";
        }
}
#undef DEFINE_CASE

static inline char *
pandecode_decode_fbd_type(enum mali_fbd_type type)
{
        if (type == MALI_SFBD)      return "SFBD";
        else if (type == MALI_MFBD) return "MFBD";
        else return "WATFBD /* XXX */";
}

static void
pandecode_replay_sfbd(uint64_t gpu_va, int job_no)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const struct mali_single_framebuffer *PANDECODE_PTR_VAR(s, mem, (mali_ptr) gpu_va);

        pandecode_log("struct mali_single_framebuffer framebuffer_%d = {\n", job_no);
        pandecode_indent++;

        pandecode_prop("unknown1 = 0x%" PRIx32, s->unknown1);
        pandecode_prop("unknown2 = 0x%" PRIx32, s->unknown2);

        pandecode_log(".format = ");
        pandecode_log_decoded_flags(fb_fmt_flag_info, s->format);
        pandecode_log_cont(",\n");

        pandecode_prop("width = MALI_POSITIVE(%" PRId16 ")", s->width + 1);
        pandecode_prop("height = MALI_POSITIVE(%" PRId16 ")", s->height + 1);

        MEMORY_PROP(s, framebuffer);
        pandecode_prop("stride = %d", s->stride);

        /* Earlier in the actual commandstream -- right before width -- but we
         * delay to flow nicer */

        pandecode_log(".clear_flags = ");
        pandecode_log_decoded_flags(clear_flag_info, s->clear_flags);
        pandecode_log_cont(",\n");

        if (s->depth_buffer | s->depth_buffer_enable) {
                MEMORY_PROP(s, depth_buffer);
                pandecode_prop("depth_buffer_enable = %s", DS_ENABLE(s->depth_buffer_enable));
        }

        if (s->stencil_buffer | s->stencil_buffer_enable) {
                MEMORY_PROP(s, stencil_buffer);
                pandecode_prop("stencil_buffer_enable = %s", DS_ENABLE(s->stencil_buffer_enable));
        }

        if (s->clear_color_1 | s->clear_color_2 | s->clear_color_3 | s->clear_color_4) {
                pandecode_prop("clear_color_1 = 0x%" PRIx32, s->clear_color_1);
                pandecode_prop("clear_color_2 = 0x%" PRIx32, s->clear_color_2);
                pandecode_prop("clear_color_3 = 0x%" PRIx32, s->clear_color_3);
                pandecode_prop("clear_color_4 = 0x%" PRIx32, s->clear_color_4);
        }

        if (s->clear_depth_1 != 0 || s->clear_depth_2 != 0 || s->clear_depth_3 != 0 || s->clear_depth_4 != 0) {
                pandecode_prop("clear_depth_1 = %f", s->clear_depth_1);
                pandecode_prop("clear_depth_2 = %f", s->clear_depth_2);
                pandecode_prop("clear_depth_3 = %f", s->clear_depth_3);
                pandecode_prop("clear_depth_4 = %f", s->clear_depth_4);
        }

        if (s->clear_stencil) {
                pandecode_prop("clear_stencil = 0x%x", s->clear_stencil);
        }

        MEMORY_PROP(s, unknown_address_0);
        MEMORY_PROP(s, unknown_address_1);
        MEMORY_PROP(s, unknown_address_2);

        pandecode_prop("resolution_check = 0x%" PRIx32, s->resolution_check);
        pandecode_prop("tiler_flags = 0x%" PRIx32, s->tiler_flags);

        MEMORY_PROP(s, tiler_heap_free);
        MEMORY_PROP(s, tiler_heap_end);

        pandecode_indent--;
        pandecode_log("};\n");

        pandecode_prop("zero0 = 0x%" PRIx64, s->zero0);
        pandecode_prop("zero1 = 0x%" PRIx64, s->zero1);
        pandecode_prop("zero2 = 0x%" PRIx32, s->zero2);
        pandecode_prop("zero4 = 0x%" PRIx32, s->zero4);

        printf(".zero3 = {");

        for (int i = 0; i < sizeof(s->zero3) / sizeof(s->zero3[0]); ++i)
                printf("%X, ", s->zero3[i]);

        printf("},\n");

        printf(".zero6 = {");

        for (int i = 0; i < sizeof(s->zero6) / sizeof(s->zero6[0]); ++i)
                printf("%X, ", s->zero6[i]);

        printf("},\n");
}

static void
pandecode_replay_swizzle(unsigned swizzle)
{
	pandecode_prop("swizzle = %s | (%s << 3) | (%s << 6) | (%s << 9)",
			pandecode_channel_name((swizzle >> 0) & 0x7),
			pandecode_channel_name((swizzle >> 3) & 0x7),
			pandecode_channel_name((swizzle >> 6) & 0x7),
			pandecode_channel_name((swizzle >> 9) & 0x7));
}

static void
pandecode_rt_format(struct mali_rt_format format)
{
        pandecode_log(".format = {\n");
        pandecode_indent++;

        pandecode_prop("unk1 = 0x%" PRIx32, format.unk1);
        pandecode_prop("unk2 = 0x%" PRIx32, format.unk2);

        pandecode_prop("nr_channels = MALI_POSITIVE(%d)",
                        MALI_NEGATIVE(format.nr_channels));

        pandecode_log(".flags = ");
        pandecode_log_decoded_flags(mfbd_fmt_flag_info, format.flags);
        pandecode_log_cont(",\n");

        pandecode_replay_swizzle(format.swizzle);

        pandecode_prop("unk4 = 0x%" PRIx32, format.unk4);

        pandecode_indent--;
        pandecode_log("},\n");
}

static void
pandecode_replay_mfbd_bfr(uint64_t gpu_va, int job_no)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const struct bifrost_framebuffer *PANDECODE_PTR_VAR(fb, mem, (mali_ptr) gpu_va);

        if (fb->sample_locations) {
                /* The blob stores all possible sample locations in a single buffer
                 * allocated on startup, and just switches the pointer when switching
                 * MSAA state. For now, we just put the data into the cmdstream, but we
                 * should do something like what the blob does with a real driver.
                 *
                 * There seem to be 32 slots for sample locations, followed by another
                 * 16. The second 16 is just the center location followed by 15 zeros
                 * in all the cases I've identified (maybe shader vs. depth/color
                 * samples?).
                 */

                struct pandecode_mapped_memory *smem = pandecode_find_mapped_gpu_mem_containing(fb->sample_locations);

                const u16 *PANDECODE_PTR_VAR(samples, smem, fb->sample_locations);

                pandecode_log("uint16_t sample_locations_%d[] = {\n", job_no);
                pandecode_indent++;

                for (int i = 0; i < 32 + 16; i++) {
                        pandecode_log("%d, %d,\n", samples[2 * i], samples[2 * i + 1]);
                }

                pandecode_indent--;
                pandecode_log("};\n");
        }

        pandecode_log("struct bifrost_framebuffer framebuffer_%d = {\n", job_no);
        pandecode_indent++;

        pandecode_prop("unk0 = 0x%x", fb->unk0);

        if (fb->sample_locations)
                pandecode_prop("sample_locations = sample_locations_%d", job_no);

        /* Assume that unknown1 and tiler_meta were emitted in the last job for
         * now */
        /*pandecode_prop("unknown1 = unknown1_%d_p", job_no - 1);
        pandecode_prop("tiler_meta = tiler_meta_%d_p", job_no - 1);*/
        MEMORY_PROP(fb, unknown1);
        MEMORY_PROP(fb, tiler_meta);

        pandecode_prop("width1 = MALI_POSITIVE(%d)", fb->width1 + 1);
        pandecode_prop("height1 = MALI_POSITIVE(%d)", fb->height1 + 1);
        pandecode_prop("width2 = MALI_POSITIVE(%d)", fb->width2 + 1);
        pandecode_prop("height2 = MALI_POSITIVE(%d)", fb->height2 + 1);

        pandecode_prop("unk1 = 0x%x", fb->unk1);
        pandecode_prop("unk2 = 0x%x", fb->unk2);
        pandecode_prop("rt_count_1 = MALI_POSITIVE(%d)", fb->rt_count_1 + 1);
        pandecode_prop("rt_count_2 = %d", fb->rt_count_2);

        pandecode_prop("unk3 = 0x%x", fb->unk3);
        pandecode_prop("clear_stencil = 0x%x", fb->clear_stencil);
        pandecode_prop("clear_depth = %f", fb->clear_depth);

        pandecode_prop("unknown2 = 0x%x", fb->unknown2);
        MEMORY_PROP(fb, scratchpad);
        MEMORY_PROP(fb, tiler_scratch_start);
        MEMORY_PROP(fb, tiler_scratch_middle);
        MEMORY_PROP(fb, tiler_heap_start);
        MEMORY_PROP(fb, tiler_heap_end);

        if (fb->zero3 || fb->zero4 || fb->zero9 || fb->zero10 || fb->zero11 || fb->zero12) {
                pandecode_msg("framebuffer zeros tripped\n");
                pandecode_prop("zero3 = 0x%" PRIx32, fb->zero3);
                pandecode_prop("zero4 = 0x%" PRIx32, fb->zero4);
                pandecode_prop("zero9 = 0x%" PRIx64, fb->zero9);
                pandecode_prop("zero10 = 0x%" PRIx64, fb->zero10);
                pandecode_prop("zero11 = 0x%" PRIx64, fb->zero11);
                pandecode_prop("zero12 = 0x%" PRIx64, fb->zero12);
        }

        pandecode_indent--;
        pandecode_log("};\n");

        gpu_va += sizeof(struct bifrost_framebuffer);

        if (fb->unk3 & MALI_MFBD_EXTRA) {
                mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
                const struct bifrost_fb_extra *PANDECODE_PTR_VAR(fbx, mem, (mali_ptr) gpu_va);

                pandecode_log("struct bifrost_fb_extra fb_extra_%d = {\n", job_no);
                pandecode_indent++;

                MEMORY_PROP(fbx, checksum);

                if (fbx->checksum_stride)
                        pandecode_prop("checksum_stride = %d", fbx->checksum_stride);

                pandecode_log(".flags = ");
                pandecode_log_decoded_flags(mfbd_extra_flag_info, fbx->flags);
                pandecode_log_cont(",\n");

                if (fbx->flags & MALI_EXTRA_AFBC_ZS) {
                        pandecode_log(".ds_afbc = {\n");
                        pandecode_indent++;

                        MEMORY_PROP((&fbx->ds_afbc), depth_stencil_afbc_metadata);
                        pandecode_prop("depth_stencil_afbc_stride = %d",
                                     fbx->ds_afbc.depth_stencil_afbc_stride);
                        MEMORY_PROP((&fbx->ds_afbc), depth_stencil);

                        if (fbx->ds_afbc.zero1 || fbx->ds_afbc.padding) {
                                pandecode_msg("Depth/stencil AFBC zeros tripped\n");
                                pandecode_prop("zero1 = 0x%" PRIx32,
                                             fbx->ds_afbc.zero1);
                                pandecode_prop("padding = 0x%" PRIx64,
                                             fbx->ds_afbc.padding);
                        }

                        pandecode_indent--;
                        pandecode_log("},\n");
                } else {
                        pandecode_log(".ds_linear = {\n");
                        pandecode_indent++;

                        if (fbx->ds_linear.depth) {
                                MEMORY_PROP((&fbx->ds_linear), depth);
                                pandecode_prop("depth_stride = %d",
                                             fbx->ds_linear.depth_stride);
                        }

                        if (fbx->ds_linear.stencil) {
                                MEMORY_PROP((&fbx->ds_linear), stencil);
                                pandecode_prop("stencil_stride = %d",
                                             fbx->ds_linear.stencil_stride);
                        }

                        if (fbx->ds_linear.depth_stride_zero ||
                                        fbx->ds_linear.stencil_stride_zero ||
                                        fbx->ds_linear.zero1 || fbx->ds_linear.zero2) {
                                pandecode_msg("Depth/stencil zeros tripped\n");
                                pandecode_prop("depth_stride_zero = 0x%x",
                                             fbx->ds_linear.depth_stride_zero);
                                pandecode_prop("stencil_stride_zero = 0x%x",
                                             fbx->ds_linear.stencil_stride_zero);
                                pandecode_prop("zero1 = 0x%" PRIx32,
                                             fbx->ds_linear.zero1);
                                pandecode_prop("zero2 = 0x%" PRIx32,
                                             fbx->ds_linear.zero2);
                        }

                        pandecode_indent--;
                        pandecode_log("},\n");
                }

                if (fbx->zero3 || fbx->zero4) {
                        pandecode_msg("fb_extra zeros tripped\n");
                        pandecode_prop("zero3 = 0x%" PRIx64, fbx->zero3);
                        pandecode_prop("zero4 = 0x%" PRIx64, fbx->zero4);
                }

                pandecode_indent--;
                pandecode_log("};\n");

                gpu_va += sizeof(struct bifrost_fb_extra);
        }

        pandecode_log("struct bifrost_render_target rts_list_%d[] = {\n", job_no);
        pandecode_indent++;

        for (int i = 0; i < MALI_NEGATIVE(fb->rt_count_1); i++) {
                mali_ptr rt_va = gpu_va + i * sizeof(struct bifrost_render_target);
                mem = pandecode_find_mapped_gpu_mem_containing(rt_va);
                const struct bifrost_render_target *PANDECODE_PTR_VAR(rt, mem, (mali_ptr) rt_va);

                pandecode_log("{\n");
                pandecode_indent++;

                pandecode_rt_format(rt->format);

                /* TODO: How the actual heck does AFBC enabling work here? */
                if (0) {
                        pandecode_log(".afbc = {\n");
                        pandecode_indent++;

                        char *a = pointer_as_memory_reference(rt->afbc.metadata);
                        pandecode_prop("metadata = %s", a);
                        free(a);

                        pandecode_prop("stride = %d", rt->afbc.stride);
                        pandecode_prop("unk = 0x%" PRIx32, rt->afbc.unk);

                        pandecode_indent--;
                        pandecode_log("},\n");
                } else {
                        pandecode_log(".chunknown = {\n");
                        pandecode_indent++;

                        pandecode_prop("unk = 0x%" PRIx64, rt->chunknown.unk);

                        char *a = pointer_as_memory_reference(rt->chunknown.pointer);
                        pandecode_prop("pointer = %s", a);
                        free(a);

                        pandecode_indent--;
                        pandecode_log("},\n");
                }

                MEMORY_PROP(rt, framebuffer);
                pandecode_prop("framebuffer_stride = %d", rt->framebuffer_stride);

                if (rt->clear_color_1 | rt->clear_color_2 | rt->clear_color_3 | rt->clear_color_4) {
                        pandecode_prop("clear_color_1 = 0x%" PRIx32, rt->clear_color_1);
                        pandecode_prop("clear_color_2 = 0x%" PRIx32, rt->clear_color_2);
                        pandecode_prop("clear_color_3 = 0x%" PRIx32, rt->clear_color_3);
                        pandecode_prop("clear_color_4 = 0x%" PRIx32, rt->clear_color_4);
                }

                if (rt->zero1 || rt->zero2 || rt->zero3) {
                        pandecode_msg("render target zeros tripped\n");
                        pandecode_prop("zero1 = 0x%" PRIx64, rt->zero1);
                        pandecode_prop("zero2 = 0x%" PRIx32, rt->zero2);
                        pandecode_prop("zero3 = 0x%" PRIx32, rt->zero3);
                }

                pandecode_indent--;
                pandecode_log("},\n");
        }

        pandecode_indent--;
        pandecode_log("};\n");
}

static void
pandecode_replay_attributes(const struct pandecode_mapped_memory *mem,
                          mali_ptr addr, int job_no, char *suffix,
                          int count, bool varying)
{
        char *prefix = varying ? "varyings" : "attributes";

        union mali_attr *attr = pandecode_fetch_gpu_mem(mem, addr, sizeof(union mali_attr) * count);

        char base[128];
        snprintf(base, sizeof(base), "%s_data_%d%s", prefix, job_no, suffix);

        for (int i = 0; i < count; ++i) {
		enum mali_attr_mode mode = attr[i].elements & 7;

		if (mode == MALI_ATTR_UNUSED)
			continue;

		mali_ptr raw_elements = attr[i].elements & ~7;

                /* TODO: Do we maybe want to dump the attribute values
                 * themselves given the specified format? Or is that too hard?
                 * */

                char *a = pointer_as_memory_reference(raw_elements);
                pandecode_log("mali_ptr %s_%d_p = %s;\n", base, i, a);
                free(a);
        }

        pandecode_log("union mali_attr %s_%d[] = {\n", prefix, job_no);
        pandecode_indent++;

        for (int i = 0; i < count; ++i) {
                pandecode_log("{\n");
                pandecode_indent++;

		pandecode_prop("elements = (%s_%d_p) | %s", base, i, pandecode_attr_mode_name(attr[i].elements & 7));
		pandecode_prop("shift = %d", attr[i].shift);
		pandecode_prop("extra_flags = %d", attr[i].extra_flags);
                pandecode_prop("stride = 0x%" PRIx32, attr[i].stride);
                pandecode_prop("size = 0x%" PRIx32, attr[i].size);
                pandecode_indent--;
                pandecode_log("}, \n");

		if ((attr[i].elements & 7) == MALI_ATTR_NPOT_DIVIDE) {
			i++;
			pandecode_log("{\n");
			pandecode_indent++;
			pandecode_prop("unk = 0x%x", attr[i].unk);
			pandecode_prop("magic_divisor = 0x%08x", attr[i].magic_divisor);
			if (attr[i].zero != 0)
				pandecode_prop("zero = 0x%x /* XXX zero tripped */", attr[i].zero);
			pandecode_prop("divisor = %d", attr[i].divisor);
			pandecode_indent--;
			pandecode_log("}, \n");
		}

        }

        pandecode_indent--;
        pandecode_log("};\n");
}

static mali_ptr
pandecode_replay_shader_address(const char *name, mali_ptr ptr)
{
        /* TODO: Decode flags */
        mali_ptr shader_ptr = ptr & ~15;

        char *a = pointer_as_memory_reference(shader_ptr);
        pandecode_prop("%s = (%s) | %d", name, a, (int) (ptr & 15));
        free(a);

        return shader_ptr;
}

static void
pandecode_replay_stencil(const char *name, const struct mali_stencil_test *stencil)
{
        const char *func = pandecode_func_name(stencil->func);
        const char *sfail = pandecode_stencil_op_name(stencil->sfail);
        const char *dpfail = pandecode_stencil_op_name(stencil->dpfail);
        const char *dppass = pandecode_stencil_op_name(stencil->dppass);

        if (stencil->zero)
                pandecode_msg("Stencil zero tripped: %X\n", stencil->zero);

        pandecode_log(".stencil_%s = {\n", name);
        pandecode_indent++;
        pandecode_prop("ref = %d", stencil->ref);
        pandecode_prop("mask = 0x%02X", stencil->mask);
        pandecode_prop("func = %s", func);
        pandecode_prop("sfail = %s", sfail);
        pandecode_prop("dpfail = %s", dpfail);
        pandecode_prop("dppass = %s", dppass);
        pandecode_indent--;
        pandecode_log("},\n");
}

static void
pandecode_replay_blend_equation(const struct mali_blend_equation *blend, const char *suffix)
{
        if (blend->zero1)
                pandecode_msg("Blend zero tripped: %X\n", blend->zero1);

        pandecode_log(".blend_equation%s = {\n", suffix);
        pandecode_indent++;

        pandecode_prop("rgb_mode = 0x%X", blend->rgb_mode);
        pandecode_prop("alpha_mode = 0x%X", blend->alpha_mode);

        pandecode_log(".color_mask = ");
        pandecode_log_decoded_flags(mask_flag_info, blend->color_mask);
        pandecode_log_cont(",\n");

        pandecode_indent--;
        pandecode_log("},\n");
}

static int
pandecode_replay_attribute_meta(int job_no, int count, const struct mali_vertex_tiler_postfix *v, bool varying, char *suffix)
{
        char base[128];
        char *prefix = varying ? "varying" : "attribute";
	unsigned max_index = 0;
        snprintf(base, sizeof(base), "%s_meta", prefix);

        pandecode_log("struct mali_attr_meta %s_%d%s[] = {\n", base, job_no, suffix);
        pandecode_indent++;

        struct mali_attr_meta *attr_meta;
        mali_ptr p = varying ? (v->varying_meta & ~0xF) : v->attribute_meta;

        struct pandecode_mapped_memory *attr_mem = pandecode_find_mapped_gpu_mem_containing(p);

        for (int i = 0; i < count; ++i, p += sizeof(struct mali_attr_meta)) {
                attr_meta = pandecode_fetch_gpu_mem(attr_mem, p,
                                                  sizeof(*attr_mem));

                pandecode_log("{\n");
                pandecode_indent++;
                pandecode_prop("index = %d", attr_meta->index);

		if (attr_meta->index > max_index)
			max_index = attr_meta->index;
		pandecode_replay_swizzle(attr_meta->swizzle);
		pandecode_prop("format = %s", pandecode_format_name(attr_meta->format));

                pandecode_prop("unknown1 = 0x%" PRIx64, (u64) attr_meta->unknown1);
                pandecode_prop("unknown3 = 0x%" PRIx64, (u64) attr_meta->unknown3);
                pandecode_prop("src_offset = 0x%" PRIx64, (u64) attr_meta->src_offset);
                pandecode_indent--;
                pandecode_log("},\n");

        }

        pandecode_indent--;
        pandecode_log("};\n");

        return max_index;
}

static void
pandecode_replay_indices(uintptr_t pindices, uint32_t index_count, int job_no)
{
        struct pandecode_mapped_memory *imem = pandecode_find_mapped_gpu_mem_containing(pindices);

        if (imem) {
                /* Indices are literally just a u32 array :) */

                uint32_t *PANDECODE_PTR_VAR(indices, imem, pindices);

                pandecode_log("uint32_t indices_%d[] = {\n", job_no);
                pandecode_indent++;

                for (unsigned i = 0; i < (index_count + 1); i += 3)
                        pandecode_log("%d, %d, %d,\n",
                                    indices[i],
                                    indices[i + 1],
                                    indices[i + 2]);

                pandecode_indent--;
                pandecode_log("};\n");
        }
}

/* return bits [lo, hi) of word */
static u32
bits(u32 word, u32 lo, u32 hi)
{
        if (hi - lo >= 32)
                return word; // avoid undefined behavior with the shift

        return (word >> lo) & ((1 << (hi - lo)) - 1);
}

static void
pandecode_replay_vertex_tiler_prefix(struct mali_vertex_tiler_prefix *p, int job_no)
{
        pandecode_log_cont("{\n");
        pandecode_indent++;

        pandecode_prop("invocation_count = %" PRIx32, p->invocation_count);
        pandecode_prop("size_y_shift = %d", p->size_y_shift);
        pandecode_prop("size_z_shift = %d", p->size_z_shift);
        pandecode_prop("workgroups_x_shift = %d", p->workgroups_x_shift);
        pandecode_prop("workgroups_y_shift = %d", p->workgroups_y_shift);
        pandecode_prop("workgroups_z_shift = %d", p->workgroups_z_shift);
        pandecode_prop("workgroups_x_shift_2 = 0x%" PRIx32, p->workgroups_x_shift_2);

        /* Decode invocation_count. See the comment before the definition of
         * invocation_count for an explanation.
         */
        pandecode_msg("size: (%d, %d, %d)\n",
                    bits(p->invocation_count, 0, p->size_y_shift) + 1,
                    bits(p->invocation_count, p->size_y_shift, p->size_z_shift) + 1,
                    bits(p->invocation_count, p->size_z_shift,
                         p->workgroups_x_shift) + 1);
        pandecode_msg("workgroups: (%d, %d, %d)\n",
                    bits(p->invocation_count, p->workgroups_x_shift,
                         p->workgroups_y_shift) + 1,
                    bits(p->invocation_count, p->workgroups_y_shift,
                         p->workgroups_z_shift) + 1,
                    bits(p->invocation_count, p->workgroups_z_shift,
                         32) + 1);

        /* TODO: Decode */
        pandecode_prop("unknown_draw = 0x%" PRIx32, p->unknown_draw);
        pandecode_prop("workgroups_x_shift_3 = 0x%" PRIx32, p->workgroups_x_shift_3);

        pandecode_prop("draw_mode = %s", pandecode_draw_mode_name(p->draw_mode));

        /* Index count only exists for tiler jobs anyway */

        if (p->index_count)
                pandecode_prop("index_count = MALI_POSITIVE(%" PRId32 ")", p->index_count + 1);

        pandecode_prop("negative_start = %d", p->negative_start);

        DYN_MEMORY_PROP(p, job_no, indices);

        if (p->zero1) {
                pandecode_msg("Zero tripped\n");
                pandecode_prop("zero1 = 0x%" PRIx32, p->zero1);
        }

        pandecode_indent--;
        pandecode_log("},\n");
}

static void
pandecode_replay_uniform_buffers(mali_ptr pubufs, int ubufs_count, int job_no)
{
        struct pandecode_mapped_memory *umem = pandecode_find_mapped_gpu_mem_containing(pubufs);

        struct mali_uniform_buffer_meta *PANDECODE_PTR_VAR(ubufs, umem, pubufs);

        for (int i = 0; i < ubufs_count; i++) {
                mali_ptr ptr = ubufs[i].ptr << 2;
                struct pandecode_mapped_memory *umem2 = pandecode_find_mapped_gpu_mem_containing(ptr);
                uint32_t *PANDECODE_PTR_VAR(ubuf, umem2, ptr);
                char name[50];
                snprintf(name, sizeof(name), "ubuf_%d", i);
                /* The blob uses ubuf 0 to upload internal stuff and
                 * uniforms that won't fit/are accessed indirectly, so
                 * it puts it in the batchbuffer.
                 */
                pandecode_log("uint32_t %s_%d[] = {\n", name, job_no);
                pandecode_indent++;

                for (int j = 0; j <= ubufs[i].size; j++) {
                        for (int k = 0; k < 4; k++) {
                                if (k == 0)
                                        pandecode_log("0x%"PRIx32", ", ubuf[4 * j + k]);
                                else
                                        pandecode_log_cont("0x%"PRIx32", ", ubuf[4 * j + k]);

                        }

                        pandecode_log_cont("\n");
                }

                pandecode_indent--;
                pandecode_log("};\n");
        }

        pandecode_log("struct mali_uniform_buffer_meta uniform_buffers_%d[] = {\n",
                    job_no);
        pandecode_indent++;

        for (int i = 0; i < ubufs_count; i++) {
                pandecode_log("{\n");
                pandecode_indent++;
                pandecode_prop("size = MALI_POSITIVE(%d)", ubufs[i].size + 1);
                pandecode_prop("ptr = ubuf_%d_%d_p >> 2", i, job_no);
                pandecode_indent--;
                pandecode_log("},\n");
        }

        pandecode_indent--;
        pandecode_log("};\n");
}

static void
pandecode_replay_scratchpad(uintptr_t pscratchpad, int job_no, char *suffix)
{

        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(pscratchpad);

        struct bifrost_scratchpad *PANDECODE_PTR_VAR(scratchpad, mem, pscratchpad);

        if (scratchpad->zero)
                pandecode_msg("XXX scratchpad zero tripped");

        pandecode_log("struct bifrost_scratchpad scratchpad_%d%s = {\n", job_no, suffix);
        pandecode_indent++;

        pandecode_prop("flags = 0x%x", scratchpad->flags);
        MEMORY_PROP(scratchpad, gpu_scratchpad);

        pandecode_indent--;
        pandecode_log("};\n");
}

static void
pandecode_shader_disassemble(mali_ptr shader_ptr, int shader_no, int type,
                           bool is_bifrost)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(shader_ptr);
        uint8_t *PANDECODE_PTR_VAR(code, mem, shader_ptr);

        /* Compute maximum possible size */
        size_t sz = mem->length - (shader_ptr - mem->gpu_va);

        /* TODO: When Bifrost is upstreamed, disassemble that too */
        if (is_bifrost) {
                pandecode_msg("Bifrost disassembler not yet upstreamed");
                return;
        }

        /* Print some boilerplate to clearly denote the assembly (which doesn't
         * obey indentation rules), and actually do the disassembly! */

        printf("\n\n");
        disassemble_midgard(code, sz);
        printf("\n\n");
}

static void
pandecode_replay_vertex_tiler_postfix_pre(const struct mali_vertex_tiler_postfix *p,
                                        int job_no, enum mali_job_type job_type,
                                        char *suffix, bool is_bifrost)
{
        mali_ptr shader_meta_ptr = (u64) (uintptr_t) (p->_shader_upper << 4);
        struct pandecode_mapped_memory *attr_mem;

        /* On Bifrost, since the tiler heap (for tiler jobs) and the scratchpad
         * are the only things actually needed from the FBD, vertex/tiler jobs
         * no longer reference the FBD -- instead, this field points to some
         * info about the scratchpad.
         */
        if (is_bifrost)
                pandecode_replay_scratchpad(p->framebuffer & ~FBD_TYPE, job_no, suffix);
        else if (p->framebuffer & MALI_MFBD)
                pandecode_replay_mfbd_bfr((u64) ((uintptr_t) p->framebuffer) & FBD_MASK, job_no);
        else
                pandecode_replay_sfbd((u64) (uintptr_t) p->framebuffer, job_no);

        int varying_count = 0, attribute_count = 0, uniform_count = 0, uniform_buffer_count = 0;
        int texture_count = 0, sampler_count = 0;

        if (shader_meta_ptr) {
                struct pandecode_mapped_memory *smem = pandecode_find_mapped_gpu_mem_containing(shader_meta_ptr);
                struct mali_shader_meta *PANDECODE_PTR_VAR(s, smem, shader_meta_ptr);

                pandecode_log("struct mali_shader_meta shader_meta_%d%s = {\n", job_no, suffix);
                pandecode_indent++;

                /* Save for dumps */
                attribute_count = s->attribute_count;
                varying_count = s->varying_count;
                texture_count = s->texture_count;
                sampler_count = s->sampler_count;

                if (is_bifrost) {
                        uniform_count = s->bifrost2.uniform_count;
                        uniform_buffer_count = s->bifrost1.uniform_buffer_count;
                } else {
                        uniform_count = s->midgard1.uniform_count;
                        /* TODO figure this out */
                        uniform_buffer_count = 1;
                }

                mali_ptr shader_ptr = pandecode_replay_shader_address("shader", s->shader);

                pandecode_prop("texture_count = %" PRId16, s->texture_count);
                pandecode_prop("sampler_count = %" PRId16, s->sampler_count);
                pandecode_prop("attribute_count = %" PRId16, s->attribute_count);
                pandecode_prop("varying_count = %" PRId16, s->varying_count);

                if (is_bifrost) {
                        pandecode_log(".bifrost1 = {\n");
                        pandecode_indent++;

                        pandecode_prop("uniform_buffer_count = %" PRId32, s->bifrost1.uniform_buffer_count);
                        pandecode_prop("unk1 = 0x%" PRIx32, s->bifrost1.unk1);

                        pandecode_indent--;
                        pandecode_log("},\n");
                } else {
                        pandecode_log(".midgard1 = {\n");
                        pandecode_indent++;

                        pandecode_prop("uniform_count = %" PRId16, s->midgard1.uniform_count);
                        pandecode_prop("work_count = %" PRId16, s->midgard1.work_count);

                        pandecode_log(".unknown1 = ");
                        pandecode_log_decoded_flags(shader_unknown1_flag_info, s->midgard1.unknown1);
                        pandecode_log_cont(",\n");

                        pandecode_prop("unknown2 = 0x%" PRIx32, s->midgard1.unknown2);

                        pandecode_indent--;
                        pandecode_log("},\n");
                }

                if (s->depth_units || s->depth_factor) {
                        if (is_bifrost)
                                pandecode_prop("depth_units = %f", s->depth_units);
                        else
                                pandecode_prop("depth_units = MALI_NEGATIVE(%f)", s->depth_units - 1.0f);

                        pandecode_prop("depth_factor = %f", s->depth_factor);
                }

                bool invert_alpha_coverage = s->alpha_coverage & 0xFFF0;
                uint16_t inverted_coverage = invert_alpha_coverage ? ~s->alpha_coverage : s->alpha_coverage;

                pandecode_prop("alpha_coverage = %sMALI_ALPHA_COVERAGE(%f)",
                             invert_alpha_coverage ? "~" : "",
                             MALI_GET_ALPHA_COVERAGE(inverted_coverage));

                pandecode_log(".unknown2_3 = ");

                int unknown2_3 = s->unknown2_3;
                int unknown2_4 = s->unknown2_4;

                /* We're not quite sure what these flags mean without the depth test, if anything */

                if (unknown2_3 & (MALI_DEPTH_TEST | MALI_DEPTH_FUNC_MASK)) {
                        const char *func = pandecode_func_name(MALI_GET_DEPTH_FUNC(unknown2_3));
                        unknown2_3 &= ~MALI_DEPTH_FUNC_MASK;

                        pandecode_log_cont("MALI_DEPTH_FUNC(%s) | ", func);
                }

                pandecode_log_decoded_flags(u3_flag_info, unknown2_3);
                pandecode_log_cont(",\n");

                pandecode_prop("stencil_mask_front = 0x%02X", s->stencil_mask_front);
                pandecode_prop("stencil_mask_back = 0x%02X", s->stencil_mask_back);

                pandecode_log(".unknown2_4 = ");
                pandecode_log_decoded_flags(u4_flag_info, unknown2_4);
                pandecode_log_cont(",\n");

                pandecode_replay_stencil("front", &s->stencil_front);
                pandecode_replay_stencil("back", &s->stencil_back);

                if (is_bifrost) {
                        pandecode_log(".bifrost2 = {\n");
                        pandecode_indent++;

                        pandecode_prop("unk3 = 0x%" PRIx32, s->bifrost2.unk3);
                        pandecode_prop("preload_regs = 0x%" PRIx32, s->bifrost2.preload_regs);
                        pandecode_prop("uniform_count = %" PRId32, s->bifrost2.uniform_count);
                        pandecode_prop("unk4 = 0x%" PRIx32, s->bifrost2.unk4);

                        pandecode_indent--;
                        pandecode_log("},\n");
                } else {
                        pandecode_log(".midgard2 = {\n");
                        pandecode_indent++;

                        pandecode_prop("unknown2_7 = 0x%" PRIx32, s->midgard2.unknown2_7);
                        pandecode_indent--;
                        pandecode_log("},\n");
                }

                pandecode_prop("unknown2_8 = 0x%" PRIx32, s->unknown2_8);

                bool blend_shader = false;

                if (!is_bifrost) {
                        if (s->unknown2_3 & MALI_HAS_BLEND_SHADER) {
                                blend_shader = true;
                                pandecode_replay_shader_address("blend_shader", s->blend_shader);
                        } else {
                                pandecode_replay_blend_equation(&s->blend_equation, "");
                        }
                }

                pandecode_indent--;
                pandecode_log("};\n");

                /* MRT blend fields are used whenever MFBD is used */

                if (job_type == JOB_TYPE_TILER) {
                        pandecode_log("struct mali_blend_meta blend_meta_%d[] = {\n",
                                    job_no);
                        pandecode_indent++;

                        int i;

                        for (i = 0; i < 4; i++) {
                                const struct mali_blend_meta *b = &s->blend_meta[i];
                                pandecode_log("{\n");
                                pandecode_indent++;

#ifndef BIFROST
                                pandecode_prop("unk1 = 0x%" PRIx64, b->unk1);

                                /* Depending on unk1, we determine if there's a
                                 * blend shader */

                                if ((b->unk1 & 0xF) >= 0x2) {
                                        blend_shader = true;
                                        pandecode_replay_shader_address("blend_shader", b->blend_shader);
                                } else {
                                        pandecode_replay_blend_equation(&b->blend_equation_1, "_1");
                                }

                                /* This is always an equation, I think. If
                                 * there's a shader, it just defaults to
                                 * REPLACE (0x122) */
                                pandecode_replay_blend_equation(&b->blend_equation_2, "_2");

                                if (b->zero2) {
                                        pandecode_msg("blend zero tripped\n");
                                        pandecode_prop("zero2 = 0x%x", b->zero2);
                                }

#else

                                pandecode_prop("unk1 = 0x%" PRIx32, b->unk1);
                                /* TODO figure out blend shader enable bit */
                                pandecode_replay_blend_equation(&b->blend_equation);
                                pandecode_prop("unk2 = 0x%" PRIx16, b->unk2);
                                pandecode_prop("index = 0x%" PRIx16, b->index);
                                pandecode_prop("unk3 = 0x%" PRIx32, b->unk3);
#endif

                                pandecode_indent--;
                                pandecode_log("},\n");

#ifdef BIFROST
                                if (b->unk2 == 3)
                                        break;
#else
                                /* TODO: What's this supposed to be? */
                                if (b->unk1 & 0x200)
                                        break;
#endif

                        }

                        pandecode_indent--;
                        pandecode_log("};\n");

                        /* This needs to be uploaded right after the
                         * shader_meta since it's technically part of the same
                         * (variable-size) structure.
                         */
                }

                pandecode_shader_disassemble(shader_ptr, job_no, job_type, is_bifrost);

                if (!is_bifrost && blend_shader)
                        pandecode_shader_disassemble(s->blend_shader & ~0xF, job_no, job_type, false);

        } else
                pandecode_msg("<no shader>\n");

        if (p->viewport) {
                struct pandecode_mapped_memory *fmem = pandecode_find_mapped_gpu_mem_containing(p->viewport);
                struct mali_viewport *PANDECODE_PTR_VAR(f, fmem, p->viewport);

                pandecode_log("struct mali_viewport viewport_%d%s = {\n", job_no, suffix);
                pandecode_indent++;

                pandecode_prop("clip_minx = %f", f->clip_minx);
                pandecode_prop("clip_miny = %f", f->clip_miny);
                pandecode_prop("clip_minz = %f", f->clip_minz);
                pandecode_prop("clip_maxx = %f", f->clip_maxx);
                pandecode_prop("clip_maxy = %f", f->clip_maxy);
                pandecode_prop("clip_maxz = %f", f->clip_maxz);

                /* Only the higher coordinates are MALI_POSITIVE scaled */

                pandecode_prop("viewport0 = { %d, %d }",
                             f->viewport0[0], f->viewport0[1]);

                pandecode_prop("viewport1 = { MALI_POSITIVE(%d), MALI_POSITIVE(%d) }",
                             f->viewport1[0] + 1, f->viewport1[1] + 1);

                pandecode_indent--;
                pandecode_log("};\n");
        }

        if (p->attribute_meta) {
                unsigned max_attr_index = pandecode_replay_attribute_meta(job_no, attribute_count, p, false, suffix);

                attr_mem = pandecode_find_mapped_gpu_mem_containing(p->attributes);
                pandecode_replay_attributes(attr_mem, p->attributes, job_no, suffix, max_attr_index + 1, false);
        }

        /* Varyings are encoded like attributes but not actually sent; we just
         * pass a zero buffer with the right stride/size set, (or whatever)
         * since the GPU will write to it itself */

        if (p->varyings) {
                attr_mem = pandecode_find_mapped_gpu_mem_containing(p->varyings);

                /* Number of descriptors depends on whether there are
                 * non-internal varyings */

                pandecode_replay_attributes(attr_mem, p->varyings, job_no, suffix, varying_count > 1 ? 4 : 1, true);
        }

        if (p->varying_meta) {
                pandecode_replay_attribute_meta(job_no, varying_count, p, true, suffix);
        }

        if (p->uniforms) {
                int rows = uniform_count, width = 4;
                size_t sz = rows * width * sizeof(float);

                struct pandecode_mapped_memory *uniform_mem = pandecode_find_mapped_gpu_mem_containing(p->uniforms);
                pandecode_fetch_gpu_mem(uniform_mem, p->uniforms, sz);
                float *PANDECODE_PTR_VAR(uniforms, uniform_mem, p->uniforms);

                pandecode_log("float uniforms_%d%s[] = {\n", job_no, suffix);

                pandecode_indent++;

                for (int row = 0; row < rows; row++) {
                        for (int i = 0; i < width; i++)
                                pandecode_log_cont("%ff, ", uniforms[i]);

                        pandecode_log_cont("\n");

                        uniforms += width;
                }

                pandecode_indent--;
                pandecode_log("};\n");
        }

        if (p->uniform_buffers) {
                pandecode_replay_uniform_buffers(p->uniform_buffers, uniform_buffer_count, job_no);
        }

        if (p->texture_trampoline) {
                struct pandecode_mapped_memory *mmem = pandecode_find_mapped_gpu_mem_containing(p->texture_trampoline);

                if (mmem) {
                        mali_ptr *PANDECODE_PTR_VAR(u, mmem, p->texture_trampoline);

                        pandecode_log("uint64_t texture_trampoline_%d[] = {\n", job_no);
                        pandecode_indent++;

                        for (int tex = 0; tex < texture_count; ++tex) {
                                mali_ptr *PANDECODE_PTR_VAR(u, mmem, p->texture_trampoline + tex * sizeof(mali_ptr));
                                char *a = pointer_as_memory_reference(*u);
                                pandecode_log("%s,\n", a);
                                free(a);
                        }

                        pandecode_indent--;
                        pandecode_log("};\n");

                        /* Now, finally, descend down into the texture descriptor */
                        for (int tex = 0; tex < texture_count; ++tex) {
                                mali_ptr *PANDECODE_PTR_VAR(u, mmem, p->texture_trampoline + tex * sizeof(mali_ptr));
                                struct pandecode_mapped_memory *tmem = pandecode_find_mapped_gpu_mem_containing(*u);

                                if (tmem) {
                                        struct mali_texture_descriptor *PANDECODE_PTR_VAR(t, tmem, *u);

                                        pandecode_log("struct mali_texture_descriptor texture_descriptor_%d_%d = {\n", job_no, tex);
                                        pandecode_indent++;

                                        pandecode_prop("width = MALI_POSITIVE(%" PRId16 ")", t->width + 1);
                                        pandecode_prop("height = MALI_POSITIVE(%" PRId16 ")", t->height + 1);
                                        pandecode_prop("depth = MALI_POSITIVE(%" PRId16 ")", t->depth + 1);

                                        pandecode_prop("unknown3 = %" PRId16, t->unknown3);
                                        pandecode_prop("unknown3A = %" PRId8, t->unknown3A);
                                        pandecode_prop("nr_mipmap_levels = %" PRId8, t->nr_mipmap_levels);

                                        struct mali_texture_format f = t->format;

                                        pandecode_log(".format = {\n");
                                        pandecode_indent++;

                                        pandecode_replay_swizzle(f.swizzle);
					pandecode_prop("format = %s", pandecode_format_name(f.format));

                                        pandecode_prop("usage1 = 0x%" PRIx32, f.usage1);
                                        pandecode_prop("is_not_cubemap = %" PRId32, f.is_not_cubemap);
                                        pandecode_prop("usage2 = 0x%" PRIx32, f.usage2);

                                        pandecode_indent--;
                                        pandecode_log("},\n");

					pandecode_replay_swizzle(t->swizzle);

                                        if (t->swizzle_zero) {
                                                /* Shouldn't happen */
                                                pandecode_msg("Swizzle zero tripped but replay will be fine anyway");
                                                pandecode_prop("swizzle_zero = %d", t->swizzle_zero);
                                        }

                                        pandecode_prop("unknown3 = 0x%" PRIx32, t->unknown3);

                                        pandecode_prop("unknown5 = 0x%" PRIx32, t->unknown5);
                                        pandecode_prop("unknown6 = 0x%" PRIx32, t->unknown6);
                                        pandecode_prop("unknown7 = 0x%" PRIx32, t->unknown7);

                                        pandecode_log(".swizzled_bitmaps = {\n");
                                        pandecode_indent++;

                                        int bitmap_count = MALI_NEGATIVE(t->nr_mipmap_levels);

                                        if (!f.is_not_cubemap) {
                                                /* Miptree for each face */
                                                bitmap_count *= 6;
                                        }

                                        int max_count = sizeof(t->swizzled_bitmaps) / sizeof(t->swizzled_bitmaps[0]);

                                        if (bitmap_count > max_count) {
                                                pandecode_msg("XXX: bitmap count tripped");
                                                bitmap_count = max_count;
                                        }

                                        for (int i = 0; i < bitmap_count; ++i) {
                                                char *a = pointer_as_memory_reference(t->swizzled_bitmaps[i]);
                                                pandecode_log("%s, \n", a);
                                                free(a);
                                        }

                                        pandecode_indent--;
                                        pandecode_log("},\n");

                                        pandecode_indent--;
                                        pandecode_log("};\n");
                                }
                        }
                }
        }

        if (p->sampler_descriptor) {
                struct pandecode_mapped_memory *smem = pandecode_find_mapped_gpu_mem_containing(p->sampler_descriptor);

                if (smem) {
                        struct mali_sampler_descriptor *s;

                        mali_ptr d = p->sampler_descriptor;

                        for (int i = 0; i < sampler_count; ++i) {
                                s = pandecode_fetch_gpu_mem(smem, d + sizeof(*s) * i, sizeof(*s));

                                pandecode_log("struct mali_sampler_descriptor sampler_descriptor_%d_%d = {\n", job_no, i);
                                pandecode_indent++;

                                /* Only the lower two bits are understood right now; the rest we display as hex */
                                pandecode_log(".filter_mode = MALI_TEX_MIN(%s) | MALI_TEX_MAG(%s) | 0x%" PRIx32",\n",
                                            MALI_FILTER_NAME(s->filter_mode & MALI_TEX_MIN_MASK),
                                            MALI_FILTER_NAME(s->filter_mode & MALI_TEX_MAG_MASK),
                                            s->filter_mode & ~3);

                                pandecode_prop("min_lod = FIXED_16(%f)", DECODE_FIXED_16(s->min_lod));
                                pandecode_prop("max_lod = FIXED_16(%f)", DECODE_FIXED_16(s->max_lod));

                                pandecode_prop("wrap_s = %s", pandecode_wrap_mode_name(s->wrap_s));
                                pandecode_prop("wrap_t = %s", pandecode_wrap_mode_name(s->wrap_t));
                                pandecode_prop("wrap_r = %s", pandecode_wrap_mode_name(s->wrap_r));

                                pandecode_prop("compare_func = %s", pandecode_alt_func_name(s->compare_func));

                                if (s->zero || s->zero2) {
                                        pandecode_msg("Zero tripped\n");
                                        pandecode_prop("zero = 0x%X, 0x%X\n", s->zero, s->zero2);
                                }

                                pandecode_prop("unknown2 = %d", s->unknown2);

                                pandecode_prop("border_color = { %f, %f, %f, %f }",
                                             s->border_color[0],
                                             s->border_color[1],
                                             s->border_color[2],
                                             s->border_color[3]);

                                pandecode_indent--;
                                pandecode_log("};\n");
                        }
                }
        }
}

static void
pandecode_replay_vertex_tiler_postfix(const struct mali_vertex_tiler_postfix *p, int job_no, bool is_bifrost)
{
        pandecode_log_cont("{\n");
        pandecode_indent++;

        MEMORY_PROP(p, position_varying);
        MEMORY_COMMENT(p, position_varying);
        DYN_MEMORY_PROP(p, job_no, uniform_buffers);
        MEMORY_COMMENT(p, uniform_buffers);
        DYN_MEMORY_PROP(p, job_no, texture_trampoline);
        MEMORY_COMMENT(p, texture_trampoline);
        DYN_MEMORY_PROP(p, job_no, sampler_descriptor);
        MEMORY_COMMENT(p, sampler_descriptor);
        DYN_MEMORY_PROP(p, job_no, uniforms);
        MEMORY_COMMENT(p, uniforms);
        DYN_MEMORY_PROP(p, job_no, attributes);
        MEMORY_COMMENT(p, attributes);
        DYN_MEMORY_PROP(p, job_no, attribute_meta);
        MEMORY_COMMENT(p, attribute_meta);
        DYN_MEMORY_PROP(p, job_no, varyings);
        MEMORY_COMMENT(p, varyings);
        DYN_MEMORY_PROP(p, job_no, varying_meta);
        MEMORY_COMMENT(p, varying_meta);
        DYN_MEMORY_PROP(p, job_no, viewport);
        MEMORY_COMMENT(p, viewport);
        DYN_MEMORY_PROP(p, job_no, occlusion_counter);
        MEMORY_COMMENT(p, occlusion_counter);
        MEMORY_COMMENT(p, framebuffer & ~1);
        pandecode_msg("%" PRIx64 "\n", p->viewport);
        pandecode_msg("%" PRIx64 "\n", p->framebuffer);

        if (is_bifrost)
                pandecode_prop("framebuffer = scratchpad_%d_p", job_no);
        else
                pandecode_prop("framebuffer = framebuffer_%d_p | %s", job_no, p->framebuffer & MALI_MFBD ? "MALI_MFBD" : "0");

        pandecode_prop("_shader_upper = (shader_meta_%d_p) >> 4", job_no);
        pandecode_prop("flags = %d", p->flags);

        pandecode_indent--;
        pandecode_log("},\n");
}

static void
pandecode_replay_vertex_only_bfr(struct bifrost_vertex_only *v)
{
        pandecode_log_cont("{\n");
        pandecode_indent++;

        pandecode_prop("unk2 = 0x%x", v->unk2);

        if (v->zero0 || v->zero1) {
                pandecode_msg("vertex only zero tripped");
                pandecode_prop("zero0 = 0x%" PRIx32, v->zero0);
                pandecode_prop("zero1 = 0x%" PRIx64, v->zero1);
        }

        pandecode_indent--;
        pandecode_log("}\n");
}

static void
pandecode_replay_tiler_heap_meta(mali_ptr gpu_va, int job_no)
{

        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const struct bifrost_tiler_heap_meta *PANDECODE_PTR_VAR(h, mem, gpu_va);

        pandecode_log("struct mali_tiler_heap_meta tiler_heap_meta_%d = {\n", job_no);
        pandecode_indent++;

        if (h->zero) {
                pandecode_msg("tiler heap zero tripped\n");
                pandecode_prop("zero = 0x%x", h->zero);
        }

        for (int i = 0; i < 12; i++) {
                if (h->zeros[i] != 0) {
                        pandecode_msg("tiler heap zero %d tripped, value %x\n",
                                    i, h->zeros[i]);
                }
        }

        pandecode_prop("heap_size = 0x%x", h->heap_size);
        MEMORY_PROP(h, tiler_heap_start);
        MEMORY_PROP(h, tiler_heap_free);

        /* this might point to the beginning of another buffer, when it's
         * really the end of the tiler heap buffer, so we have to be careful
         * here.
         */
        char *a = pointer_as_memory_reference(h->tiler_heap_end - 1);
        pandecode_prop("tiler_heap_end = %s + 1", a);
        free(a);

        pandecode_indent--;
        pandecode_log("};\n");
}

static void
pandecode_replay_tiler_meta(mali_ptr gpu_va, int job_no)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);
        const struct bifrost_tiler_meta *PANDECODE_PTR_VAR(t, mem, gpu_va);

        pandecode_replay_tiler_heap_meta(t->tiler_heap_meta, job_no);

        pandecode_log("struct mali_tiler_meta tiler_meta_%d = {\n", job_no);
        pandecode_indent++;

        if (t->zero0 || t->zero1) {
                pandecode_msg("tiler meta zero tripped");
                pandecode_prop("zero0 = 0x%" PRIx64, t->zero0);
                pandecode_prop("zero1 = 0x%" PRIx64, t->zero1);
        }

        pandecode_prop("unk = 0x%x", t->unk);
        pandecode_prop("width = MALI_POSITIVE(%d)", t->width + 1);
        pandecode_prop("height = MALI_POSITIVE(%d)", t->height + 1);
        DYN_MEMORY_PROP(t, job_no, tiler_heap_meta);

        for (int i = 0; i < 12; i++) {
                if (t->zeros[i] != 0) {
                        pandecode_msg("tiler heap zero %d tripped, value %" PRIx64 "\n",
                                    i, t->zeros[i]);
                }
        }

        pandecode_indent--;
        pandecode_log("};\n");
}

static void
pandecode_replay_gl_enables(uint32_t gl_enables, int job_type)
{
        pandecode_log(".gl_enables = ");

        if (job_type == JOB_TYPE_TILER) {
                pandecode_log_cont("MALI_FRONT_FACE(MALI_%s) | ",
                                 gl_enables & MALI_FRONT_FACE(MALI_CW) ? "CW" : "CCW");

                gl_enables &= ~(MALI_FRONT_FACE(1));
        }

        pandecode_log_decoded_flags(gl_enable_flag_info, gl_enables);

        pandecode_log_cont(",\n");
}

static void
pandecode_replay_primitive_size(union midgard_primitive_size u, bool constant)
{
        pandecode_log(".primitive_size = {\n");
        pandecode_indent++;

        if (constant) {
                pandecode_prop("constant = %f", u.constant);
        } else {
                MEMORY_PROP((&u), pointer);
        }

        pandecode_indent--;
        pandecode_log("},\n");
}

static void
pandecode_replay_tiler_only_bfr(const struct bifrost_tiler_only *t, int job_no)
{
        pandecode_log_cont("{\n");
        pandecode_indent++;

        /* TODO: gl_PointSize on Bifrost */
        pandecode_replay_primitive_size(t->primitive_size, true);

        DYN_MEMORY_PROP(t, job_no, tiler_meta);
        pandecode_replay_gl_enables(t->gl_enables, JOB_TYPE_TILER);

        if (t->zero1 || t->zero2 || t->zero3 || t->zero4 || t->zero5
                        || t->zero6 || t->zero7 || t->zero8) {
                pandecode_msg("tiler only zero tripped");
                pandecode_prop("zero1 = 0x%" PRIx64, t->zero1);
                pandecode_prop("zero2 = 0x%" PRIx64, t->zero2);
                pandecode_prop("zero3 = 0x%" PRIx64, t->zero3);
                pandecode_prop("zero4 = 0x%" PRIx64, t->zero4);
                pandecode_prop("zero5 = 0x%" PRIx64, t->zero5);
                pandecode_prop("zero6 = 0x%" PRIx64, t->zero6);
                pandecode_prop("zero7 = 0x%" PRIx32, t->zero7);
                pandecode_prop("zero8 = 0x%" PRIx64, t->zero8);
        }

        pandecode_indent--;
        pandecode_log("},\n");
}

static int
pandecode_replay_vertex_job_bfr(const struct mali_job_descriptor_header *h,
                              const struct pandecode_mapped_memory *mem,
                              mali_ptr payload, int job_no)
{
        struct bifrost_payload_vertex *PANDECODE_PTR_VAR(v, mem, payload);

        pandecode_replay_vertex_tiler_postfix_pre(&v->postfix, job_no, h->job_type, "", true);

        pandecode_log("struct bifrost_payload_vertex payload_%d = {\n", job_no);
        pandecode_indent++;

        pandecode_log(".prefix = ");
        pandecode_replay_vertex_tiler_prefix(&v->prefix, job_no);

        pandecode_log(".vertex = ");
        pandecode_replay_vertex_only_bfr(&v->vertex);

        pandecode_log(".postfix = ");
        pandecode_replay_vertex_tiler_postfix(&v->postfix, job_no, true);

        pandecode_indent--;
        pandecode_log("};\n");

        return sizeof(*v);
}

static int
pandecode_replay_tiler_job_bfr(const struct mali_job_descriptor_header *h,
                             const struct pandecode_mapped_memory *mem,
                             mali_ptr payload, int job_no)
{
        struct bifrost_payload_tiler *PANDECODE_PTR_VAR(t, mem, payload);

        pandecode_replay_vertex_tiler_postfix_pre(&t->postfix, job_no, h->job_type, "", true);

        pandecode_replay_indices(t->prefix.indices, t->prefix.index_count, job_no);
        pandecode_replay_tiler_meta(t->tiler.tiler_meta, job_no);

        pandecode_log("struct bifrost_payload_tiler payload_%d = {\n", job_no);
        pandecode_indent++;

        pandecode_log(".prefix = ");
        pandecode_replay_vertex_tiler_prefix(&t->prefix, job_no);

        pandecode_log(".tiler = ");
        pandecode_replay_tiler_only_bfr(&t->tiler, job_no);

        pandecode_log(".postfix = ");
        pandecode_replay_vertex_tiler_postfix(&t->postfix, job_no, true);

        pandecode_indent--;
        pandecode_log("};\n");

        return sizeof(*t);
}

static int
pandecode_replay_vertex_or_tiler_job_mdg(const struct mali_job_descriptor_header *h,
                                       const struct pandecode_mapped_memory *mem,
                                       mali_ptr payload, int job_no)
{
        struct midgard_payload_vertex_tiler *PANDECODE_PTR_VAR(v, mem, payload);

        char *a = pointer_as_memory_reference(payload);
        pandecode_msg("vt payload: %s\n", a);
        free(a);

        pandecode_replay_vertex_tiler_postfix_pre(&v->postfix, job_no, h->job_type, "", false);

        pandecode_replay_indices(v->prefix.indices, v->prefix.index_count, job_no);

        pandecode_log("struct midgard_payload_vertex_tiler payload_%d = {\n", job_no);
        pandecode_indent++;

        bool has_primitive_pointer = v->prefix.unknown_draw & MALI_DRAW_VARYING_SIZE;
        pandecode_replay_primitive_size(v->primitive_size, !has_primitive_pointer);

        pandecode_log(".prefix = ");
        pandecode_replay_vertex_tiler_prefix(&v->prefix, job_no);

        pandecode_replay_gl_enables(v->gl_enables, h->job_type);
        pandecode_prop("draw_start = %d", v->draw_start);

#ifndef __LP64__

        if (v->zero3) {
                pandecode_msg("Zero tripped\n");
                pandecode_prop("zero3 = 0x%" PRIx32, v->zero3);
        }

#endif

        if (v->zero5) {
                pandecode_msg("Zero tripped\n");
                pandecode_prop("zero5 = 0x%" PRIx64, v->zero5);
        }

        pandecode_log(".postfix = ");
        pandecode_replay_vertex_tiler_postfix(&v->postfix, job_no, false);

        pandecode_indent--;
        pandecode_log("};\n");

        return sizeof(*v);
}

static int
pandecode_replay_fragment_job(const struct pandecode_mapped_memory *mem,
                            mali_ptr payload, int job_no,
                            bool is_bifrost)
{
        const struct mali_payload_fragment *PANDECODE_PTR_VAR(s, mem, payload);

        bool fbd_dumped = false;

        if (!is_bifrost && (s->framebuffer & FBD_TYPE) == MALI_SFBD) {
                /* Only SFBDs are understood, not MFBDs. We're speculating,
                 * based on the versioning, kernel code, etc, that the
                 * difference is between Single FrameBuffer Descriptor and
                 * Multiple FrmaeBuffer Descriptor; the change apparently lines
                 * up with multi-framebuffer support being added (T7xx onwards,
                 * including Gxx). In any event, there's some field shuffling
                 * that we haven't looked into yet. */

                pandecode_replay_sfbd(s->framebuffer & FBD_MASK, job_no);
                fbd_dumped = true;
        } else if ((s->framebuffer & FBD_TYPE) == MALI_MFBD) {
                /* We don't know if Bifrost supports SFBD's at all, since the
                 * driver never uses them. And the format is different from
                 * Midgard anyways, due to the tiler heap and scratchpad being
                 * moved out into separate structures, so it's not clear what a
                 * Bifrost SFBD would even look like without getting an actual
                 * trace, which appears impossible.
                 */

                pandecode_replay_mfbd_bfr(s->framebuffer & FBD_MASK, job_no);
                fbd_dumped = true;
        }

        uintptr_t p = (uintptr_t) s->framebuffer & FBD_MASK;

        pandecode_log("struct mali_payload_fragment payload_%d = {\n", job_no);
        pandecode_indent++;

        /* See the comments by the macro definitions for mathematical context
         * on why this is so weird */

        if (MALI_TILE_COORD_FLAGS(s->max_tile_coord) || MALI_TILE_COORD_FLAGS(s->min_tile_coord))
                pandecode_msg("Tile coordinate flag missed, replay wrong\n");

        pandecode_prop("min_tile_coord = MALI_COORDINATE_TO_TILE_MIN(%d, %d)",
                     MALI_TILE_COORD_X(s->min_tile_coord) << MALI_TILE_SHIFT,
                     MALI_TILE_COORD_Y(s->min_tile_coord) << MALI_TILE_SHIFT);

        pandecode_prop("max_tile_coord = MALI_COORDINATE_TO_TILE_MAX(%d, %d)",
                     (MALI_TILE_COORD_X(s->max_tile_coord) + 1) << MALI_TILE_SHIFT,
                     (MALI_TILE_COORD_Y(s->max_tile_coord) + 1) << MALI_TILE_SHIFT);

        /* If the FBD was just decoded, we can refer to it by pointer. If not,
         * we have to fallback on offsets. */

        const char *fbd_type = s->framebuffer & MALI_MFBD ? "MALI_MFBD" : "MALI_SFBD";

        if (fbd_dumped)
                pandecode_prop("framebuffer = framebuffer_%d_p | %s", job_no, fbd_type);
        else
                pandecode_prop("framebuffer = %s | %s", pointer_as_memory_reference(p), fbd_type);

        pandecode_indent--;
        pandecode_log("};\n");

        return sizeof(*s);
}

static int job_descriptor_number = 0;

int
pandecode_replay_jc(mali_ptr jc_gpu_va, bool bifrost)
{
        struct mali_job_descriptor_header *h;

        int start_number = 0;

        bool first = true;
        bool last_size;

        do {
                struct pandecode_mapped_memory *mem =
                        pandecode_find_mapped_gpu_mem_containing(jc_gpu_va);

                void *payload;

                h = PANDECODE_PTR(mem, jc_gpu_va, struct mali_job_descriptor_header);

                /* On Midgard, for 32-bit jobs except for fragment jobs, the
                 * high 32-bits of the 64-bit pointer are reused to store
                 * something else.
                 */
                int offset = h->job_descriptor_size == MALI_JOB_32 &&
                             h->job_type != JOB_TYPE_FRAGMENT ? 4 : 0;
                mali_ptr payload_ptr = jc_gpu_va + sizeof(*h) - offset;

                payload = pandecode_fetch_gpu_mem(mem, payload_ptr,
                                                MALI_PAYLOAD_SIZE);

                int job_no = job_descriptor_number++;

                if (first)
                        start_number = job_no;

                pandecode_log("struct mali_job_descriptor_header job_%d = {\n", job_no);
                pandecode_indent++;

                pandecode_prop("job_type = %s", pandecode_job_type_name(h->job_type));

                /* Save for next job fixing */
                last_size = h->job_descriptor_size;

                if (h->job_descriptor_size)
                        pandecode_prop("job_descriptor_size = %d", h->job_descriptor_size);

                if (h->exception_status)
                        pandecode_prop("exception_status = %d", h->exception_status);

                if (h->first_incomplete_task)
                        pandecode_prop("first_incomplete_task = %d", h->first_incomplete_task);

                if (h->fault_pointer)
                        pandecode_prop("fault_pointer = 0x%" PRIx64, h->fault_pointer);

                if (h->job_barrier)
                        pandecode_prop("job_barrier = %d", h->job_barrier);

                pandecode_prop("job_index = %d", h->job_index);

                if (h->unknown_flags)
                        pandecode_prop("unknown_flags = %d", h->unknown_flags);

                if (h->job_dependency_index_1)
                        pandecode_prop("job_dependency_index_1 = %d", h->job_dependency_index_1);

                if (h->job_dependency_index_2)
                        pandecode_prop("job_dependency_index_2 = %d", h->job_dependency_index_2);

                pandecode_indent--;
                pandecode_log("};\n");

                /* Do not touch the field yet -- decode the payload first, and
                 * don't touch that either. This is essential for the uploads
                 * to occur in sequence and therefore be dynamically allocated
                 * correctly. Do note the size, however, for that related
                 * reason. */

                switch (h->job_type) {
                case JOB_TYPE_SET_VALUE: {
                        struct mali_payload_set_value *s = payload;

                        pandecode_log("struct mali_payload_set_value payload_%d = {\n", job_no);
                        pandecode_indent++;
                        MEMORY_PROP(s, out);
                        pandecode_prop("unknown = 0x%" PRIX64, s->unknown);
                        pandecode_indent--;
                        pandecode_log("};\n");

                        break;
                }

                case JOB_TYPE_TILER:
                case JOB_TYPE_VERTEX:
                case JOB_TYPE_COMPUTE:
                        if (bifrost) {
                                if (h->job_type == JOB_TYPE_TILER)
                                        pandecode_replay_tiler_job_bfr(h, mem, payload_ptr, job_no);
                                else
                                        pandecode_replay_vertex_job_bfr(h, mem, payload_ptr, job_no);
                        } else
                                pandecode_replay_vertex_or_tiler_job_mdg(h, mem, payload_ptr, job_no);

                        break;

                case JOB_TYPE_FRAGMENT:
                        pandecode_replay_fragment_job(mem, payload_ptr, job_no, bifrost);
                        break;

                default:
                        break;
                }

                /* Handle linkage */

                if (!first) {
                        pandecode_log("((struct mali_job_descriptor_header *) (uintptr_t) job_%d_p)->", job_no - 1);

                        if (last_size)
                                pandecode_log_cont("next_job_64 = job_%d_p;\n\n", job_no);
                        else
                                pandecode_log_cont("next_job_32 = (u32) (uintptr_t) job_%d_p;\n\n", job_no);
                }

                first = false;

        } while ((jc_gpu_va = h->job_descriptor_size ? h->next_job_64 : h->next_job_32));

        return start_number;
}
