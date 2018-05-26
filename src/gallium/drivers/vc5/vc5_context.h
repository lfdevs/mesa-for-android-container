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

#ifndef VC5_CONTEXT_H
#define VC5_CONTEXT_H

#ifdef V3D_VERSION
#include "broadcom/common/v3d_macros.h"
#endif

#include <stdio.h>

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/bitset.h"
#include "util/slab.h"
#include "xf86drm.h"
#include "vc5_drm.h"
#include "vc5_screen.h"

struct vc5_job;
struct vc5_bo;
void vc5_job_add_bo(struct vc5_job *job, struct vc5_bo *bo);

#define __user
#include "vc5_drm.h"
#include "vc5_bufmgr.h"
#include "vc5_resource.h"
#include "vc5_cl.h"

#ifdef USE_VC5_SIMULATOR
#define using_vc5_simulator true
#else
#define using_vc5_simulator false
#endif

#define VC5_DIRTY_BLEND         (1 <<  0)
#define VC5_DIRTY_RASTERIZER    (1 <<  1)
#define VC5_DIRTY_ZSA           (1 <<  2)
#define VC5_DIRTY_FRAGTEX       (1 <<  3)
#define VC5_DIRTY_VERTTEX       (1 <<  4)

#define VC5_DIRTY_BLEND_COLOR   (1 <<  7)
#define VC5_DIRTY_STENCIL_REF   (1 <<  8)
#define VC5_DIRTY_SAMPLE_MASK   (1 <<  9)
#define VC5_DIRTY_FRAMEBUFFER   (1 << 10)
#define VC5_DIRTY_STIPPLE       (1 << 11)
#define VC5_DIRTY_VIEWPORT      (1 << 12)
#define VC5_DIRTY_CONSTBUF      (1 << 13)
#define VC5_DIRTY_VTXSTATE      (1 << 14)
#define VC5_DIRTY_VTXBUF        (1 << 15)
#define VC5_DIRTY_SCISSOR       (1 << 17)
#define VC5_DIRTY_FLAT_SHADE_FLAGS (1 << 18)
#define VC5_DIRTY_PRIM_MODE     (1 << 19)
#define VC5_DIRTY_CLIP          (1 << 20)
#define VC5_DIRTY_UNCOMPILED_VS (1 << 21)
#define VC5_DIRTY_UNCOMPILED_FS (1 << 22)
#define VC5_DIRTY_COMPILED_CS   (1 << 23)
#define VC5_DIRTY_COMPILED_VS   (1 << 24)
#define VC5_DIRTY_COMPILED_FS   (1 << 25)
#define VC5_DIRTY_FS_INPUTS     (1 << 26)
#define VC5_DIRTY_STREAMOUT     (1 << 27)
#define VC5_DIRTY_OQ            (1 << 28)

#define VC5_MAX_FS_INPUTS 64

struct vc5_sampler_view {
        struct pipe_sampler_view base;
        uint32_t p0;
        uint32_t p1;
        /* Precomputed swizzles to pass in to the shader key. */
        uint8_t swizzle[4];

        uint8_t texture_shader_state[32];
        /* V3D 4.x: Texture state struct. */
        struct vc5_bo *bo;
};

struct vc5_sampler_state {
        struct pipe_sampler_state base;
        uint32_t p0;
        uint32_t p1;

        /* V3D 3.x: Packed texture state. */
        uint8_t texture_shader_state[32];
        /* V3D 4.x: Sampler state struct. */
        struct vc5_bo *bo;
};

struct vc5_texture_stateobj {
        struct pipe_sampler_view *textures[PIPE_MAX_SAMPLERS];
        unsigned num_textures;
        struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
        unsigned num_samplers;
        struct vc5_cl_reloc texture_state[PIPE_MAX_SAMPLERS];
};

struct vc5_shader_uniform_info {
        enum quniform_contents *contents;
        uint32_t *data;
        uint32_t count;
};

struct vc5_uncompiled_shader {
        /** A name for this program, so you can track it in shader-db output. */
        uint32_t program_id;
        /** How many variants of this program were compiled, for shader-db. */
        uint32_t compiled_variant_count;
        struct pipe_shader_state base;
        uint32_t num_tf_outputs;
        struct v3d_varying_slot *tf_outputs;
        uint16_t tf_specs[16];
        uint16_t tf_specs_psiz[16];
        uint32_t num_tf_specs;

        /**
         * Flag for if the NIR in this shader originally came from TGSI.  If
         * so, we need to do some fixups at compile time, due to missing
         * information in TGSI that exists in NIR.
         */
        bool was_tgsi;
};

struct vc5_compiled_shader {
        struct vc5_bo *bo;

        union {
                struct v3d_prog_data *base;
                struct v3d_vs_prog_data *vs;
                struct v3d_fs_prog_data *fs;
        } prog_data;

        /**
         * VC5_DIRTY_* flags that, when set in vc5->dirty, mean that the
         * uniforms have to be rewritten (and therefore the shader state
         * reemitted).
         */
        uint32_t uniform_dirty_bits;
};

struct vc5_program_stateobj {
        struct vc5_uncompiled_shader *bind_vs, *bind_fs;
        struct vc5_compiled_shader *cs, *vs, *fs;

        struct vc5_bo *spill_bo;
        int spill_size_per_thread;
};

struct vc5_constbuf_stateobj {
        struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
        uint32_t enabled_mask;
        uint32_t dirty_mask;
};

struct vc5_vertexbuf_stateobj {
        struct pipe_vertex_buffer vb[PIPE_MAX_ATTRIBS];
        unsigned count;
        uint32_t enabled_mask;
        uint32_t dirty_mask;
};

struct vc5_vertex_stateobj {
        struct pipe_vertex_element pipe[VC5_MAX_ATTRIBUTES];
        unsigned num_elements;

        uint8_t attrs[12 * VC5_MAX_ATTRIBUTES];
        struct vc5_bo *default_attribute_values;
};

struct vc5_streamout_stateobj {
        struct pipe_stream_output_target *targets[PIPE_MAX_SO_BUFFERS];
        unsigned num_targets;
};

/* Hash table key for vc5->jobs */
struct vc5_job_key {
        struct pipe_surface *cbufs[4];
        struct pipe_surface *zsbuf;
};

enum vc5_ez_state {
        VC5_EZ_UNDECIDED = 0,
        VC5_EZ_GT_GE,
        VC5_EZ_LT_LE,
        VC5_EZ_DISABLED,
};

/**
 * A complete bin/render job.
 *
 * This is all of the state necessary to submit a bin/render to the kernel.
 * We want to be able to have multiple in progress at a time, so that we don't
 * need to flush an existing CL just to switch to rendering to a new render
 * target (which would mean reading back from the old render target when
 * starting to render to it again).
 */
struct vc5_job {
        struct vc5_context *vc5;
        struct vc5_cl bcl;
        struct vc5_cl rcl;
        struct vc5_cl indirect;
        struct vc5_bo *tile_alloc;
        struct vc5_bo *tile_state;
        uint32_t shader_rec_count;

        struct drm_vc5_submit_cl submit;

        /**
         * Set of all BOs referenced by the job.  This will be used for making
         * the list of BOs that the kernel will need to have paged in to
         * execute our job.
         */
        struct set *bos;

        /** Sum of the sizes of the BOs referenced by the job. */
        uint32_t referenced_size;

        struct set *write_prscs;

        /* Size of the submit.bo_handles array. */
        uint32_t bo_handles_size;

        /** @{ Surfaces to submit rendering for. */
        struct pipe_surface *cbufs[4];
        struct pipe_surface *zsbuf;
        /** @} */
        /** @{
         * Bounding box of the scissor across all queued drawing.
         *
         * Note that the max values are exclusive.
         */
        uint32_t draw_min_x;
        uint32_t draw_min_y;
        uint32_t draw_max_x;
        uint32_t draw_max_y;
        /** @} */
        /** @{
         * Width/height of the color framebuffer being rendered to,
         * for VC5_TILE_RENDERING_MODE_CONFIG.
        */
        uint32_t draw_width;
        uint32_t draw_height;
        /** @} */
        /** @{ Tile information, depending on MSAA and float color buffer. */
        uint32_t draw_tiles_x; /** @< Number of tiles wide for framebuffer. */
        uint32_t draw_tiles_y; /** @< Number of tiles high for framebuffer. */

        uint32_t tile_width; /** @< Width of a tile. */
        uint32_t tile_height; /** @< Height of a tile. */
        /** maximum internal_bpp of all color render targets. */
        uint32_t internal_bpp;

        /** Whether the current rendering is in a 4X MSAA tile buffer. */
        bool msaa;
        /** @} */

        /* Bitmask of PIPE_CLEAR_* of buffers that were cleared before the
         * first rendering.
         */
        uint32_t cleared;
        /* Bitmask of PIPE_CLEAR_* of buffers that have been rendered to
         * (either clears or draws).
         */
        uint32_t resolve;
        uint32_t clear_color[4][4];
        float clear_z;
        uint8_t clear_s;

        /**
         * Set if some drawing (triangles, blits, or just a glClear()) has
         * been done to the FBO, meaning that we need to
         * DRM_IOCTL_VC5_SUBMIT_CL.
         */
        bool needs_flush;

        /**
         * Set if there is a nonzero address for OCCLUSION_QUERY_COUNTER.  If
         * so, we need to disable it and flush before ending the CL, to keep
         * the next tile from starting with it enabled.
         */
        bool oq_enabled;

        /**
         * Set when a packet enabling TF on all further primitives has been
         * emitted.
         */
        bool tf_enabled;

        /**
         * Current EZ state for drawing. Updated at the start of draw after
         * we've decided on the shader being rendered.
         */
        enum vc5_ez_state ez_state;
        /**
         * The first EZ state that was used for drawing with a decided EZ
         * direction (so either UNDECIDED, GT, or LT).
         */
        enum vc5_ez_state first_ez_state;

        /**
         * Number of draw calls (not counting full buffer clears) queued in
         * the current job.
         */
        uint32_t draw_calls_queued;

        struct vc5_job_key key;
};

struct vc5_context {
        struct pipe_context base;

        int fd;
        struct vc5_screen *screen;

        /** The 3D rendering job for the currently bound FBO. */
        struct vc5_job *job;

        /* Map from struct vc5_job_key to the job for that FBO.
         */
        struct hash_table *jobs;

        /**
         * Map from vc5_resource to a job writing to that resource.
         *
         * Primarily for flushing jobs rendering to textures that are now
         * being read from.
         */
        struct hash_table *write_jobs;

        struct slab_child_pool transfer_pool;
        struct blitter_context *blitter;

        /** bitfield of VC5_DIRTY_* */
        uint32_t dirty;

        struct primconvert_context *primconvert;

        struct hash_table *fs_cache, *vs_cache;
        uint32_t next_uncompiled_program_id;
        uint64_t next_compiled_program_id;

        struct vc5_compiler_state *compiler_state;

        uint8_t prim_mode;

        /** Maximum index buffer valid for the current shader_rec. */
        uint32_t max_index;

        /** Sync object that our RCL will update as its out_sync. */
        uint32_t out_sync;

        struct u_upload_mgr *uploader;

        /** @{ Current pipeline state objects */
        struct pipe_scissor_state scissor;
        struct pipe_blend_state *blend;
        struct vc5_rasterizer_state *rasterizer;
        struct vc5_depth_stencil_alpha_state *zsa;

        struct vc5_texture_stateobj verttex, fragtex;

        struct vc5_program_stateobj prog;

        struct vc5_vertex_stateobj *vtx;

        struct {
                struct pipe_blend_color f;
                uint16_t hf[4];
        } blend_color;
        struct pipe_stencil_ref stencil_ref;
        unsigned sample_mask;
        struct pipe_framebuffer_state framebuffer;

        /* Per render target, whether we should swap the R and B fields in the
         * shader's color output and in blending.  If render targets disagree
         * on the R/B swap and use the constant color, then we would need to
         * fall back to in-shader blending.
         */
        uint8_t swap_color_rb;

        /* Per render target, whether we should treat the dst alpha values as
         * one in blending.
         *
         * For RGBX formats, the tile buffer's alpha channel will be
         * undefined.
         */
        uint8_t blend_dst_alpha_one;

        bool active_queries;

        uint32_t tf_prims_generated;
        uint32_t prims_generated;

        struct pipe_poly_stipple stipple;
        struct pipe_clip_state clip;
        struct pipe_viewport_state viewport;
        struct vc5_constbuf_stateobj constbuf[PIPE_SHADER_TYPES];
        struct vc5_vertexbuf_stateobj vertexbuf;
        struct vc5_streamout_stateobj streamout;
        struct vc5_bo *current_oq;
        /** @} */
};

struct vc5_rasterizer_state {
        struct pipe_rasterizer_state base;

        /* VC5_CONFIGURATION_BITS */
        uint8_t config_bits[3];

        float point_size;

        /**
         * Half-float (1/8/7 bits) value of polygon offset units for
         * VC5_PACKET_DEPTH_OFFSET
         */
        uint16_t offset_units;
        /**
         * Half-float (1/8/7 bits) value of polygon offset scale for
         * VC5_PACKET_DEPTH_OFFSET
         */
        uint16_t offset_factor;
};

struct vc5_depth_stencil_alpha_state {
        struct pipe_depth_stencil_alpha_state base;

        enum vc5_ez_state ez_state;

        /** Uniforms for stencil state.
         *
         * Index 0 is either the front config, or the front-and-back config.
         * Index 1 is the back config if doing separate back stencil.
         * Index 2 is the writemask config if it's not a common mask value.
         */
        uint32_t stencil_uniforms[3];

        uint8_t stencil_front[6];
        uint8_t stencil_back[6];
};

#define perf_debug(...) do {                            \
        if (unlikely(V3D_DEBUG & V3D_DEBUG_PERF))       \
                fprintf(stderr, __VA_ARGS__);           \
} while (0)

static inline struct vc5_context *
vc5_context(struct pipe_context *pcontext)
{
        return (struct vc5_context *)pcontext;
}

static inline struct vc5_sampler_view *
vc5_sampler_view(struct pipe_sampler_view *psview)
{
        return (struct vc5_sampler_view *)psview;
}

static inline struct vc5_sampler_state *
vc5_sampler_state(struct pipe_sampler_state *psampler)
{
        return (struct vc5_sampler_state *)psampler;
}

struct pipe_context *vc5_context_create(struct pipe_screen *pscreen,
                                        void *priv, unsigned flags);
void vc5_program_init(struct pipe_context *pctx);
void vc5_program_fini(struct pipe_context *pctx);
void vc5_query_init(struct pipe_context *pctx);

void vc5_simulator_init(struct vc5_screen *screen);
void vc5_simulator_destroy(struct vc5_screen *screen);
int vc5_simulator_flush(struct vc5_context *vc5,
                        struct drm_vc5_submit_cl *args,
                        struct vc5_job *job);
int vc5_simulator_ioctl(int fd, unsigned long request, void *arg);
void vc5_simulator_open_from_handle(int fd, uint32_t winsys_stride,
                                    int handle, uint32_t size);

static inline int
vc5_ioctl(int fd, unsigned long request, void *arg)
{
        if (using_vc5_simulator)
                return vc5_simulator_ioctl(fd, request, arg);
        else
                return drmIoctl(fd, request, arg);
}

void vc5_set_shader_uniform_dirty_flags(struct vc5_compiled_shader *shader);
struct vc5_cl_reloc vc5_write_uniforms(struct vc5_context *vc5,
                                       struct vc5_compiled_shader *shader,
                                       struct vc5_constbuf_stateobj *cb,
                                       struct vc5_texture_stateobj *texstate);

void vc5_flush(struct pipe_context *pctx);
void vc5_job_init(struct vc5_context *vc5);
struct vc5_job *vc5_get_job(struct vc5_context *vc5,
                            struct pipe_surface **cbufs,
                            struct pipe_surface *zsbuf);
struct vc5_job *vc5_get_job_for_fbo(struct vc5_context *vc5);
void vc5_job_add_bo(struct vc5_job *job, struct vc5_bo *bo);
void vc5_job_add_write_resource(struct vc5_job *job, struct pipe_resource *prsc);
void vc5_job_submit(struct vc5_context *vc5, struct vc5_job *job);
void vc5_flush_jobs_writing_resource(struct vc5_context *vc5,
                                     struct pipe_resource *prsc);
void vc5_flush_jobs_reading_resource(struct vc5_context *vc5,
                                     struct pipe_resource *prsc);
void vc5_update_compiled_shaders(struct vc5_context *vc5, uint8_t prim_mode);

bool vc5_rt_format_supported(const struct v3d_device_info *devinfo,
                             enum pipe_format f);
bool vc5_tex_format_supported(const struct v3d_device_info *devinfo,
                              enum pipe_format f);
uint8_t vc5_get_rt_format(const struct v3d_device_info *devinfo, enum pipe_format f);
uint8_t vc5_get_tex_format(const struct v3d_device_info *devinfo, enum pipe_format f);
uint8_t vc5_get_tex_return_size(const struct v3d_device_info *devinfo,
                                enum pipe_format f,
                                enum pipe_tex_compare compare);
uint8_t vc5_get_tex_return_channels(const struct v3d_device_info *devinfo,
                                    enum pipe_format f);
const uint8_t *vc5_get_format_swizzle(const struct v3d_device_info *devinfo,
                                      enum pipe_format f);
void vc5_get_internal_type_bpp_for_output_format(const struct v3d_device_info *devinfo,
                                                 uint32_t format,
                                                 uint32_t *type,
                                                 uint32_t *bpp);

void vc5_init_query_functions(struct vc5_context *vc5);
void vc5_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info);
void vc5_blitter_save(struct vc5_context *vc5);

struct vc5_fence *vc5_fence_create(struct vc5_context *vc5);

#ifdef v3dX
#  include "v3dx_context.h"
#else
#  define v3dX(x) v3d33_##x
#  include "v3dx_context.h"
#  undef v3dX

#  define v3dX(x) v3d41_##x
#  include "v3dx_context.h"
#  undef v3dX
#endif

#endif /* VC5_CONTEXT_H */
