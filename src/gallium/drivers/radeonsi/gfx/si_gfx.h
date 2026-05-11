/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_GFX_H
#define SI_GFX_H

#include "si_pipe.h"
#include "util/mesa-blake3.h"
#include "util/u_stub_gfx_compute.h"
#include "ac_sqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

struct si_screen;
struct si_shader;
struct si_shader_selector;
struct si_context;
struct ac_llvm_compiler;
struct nir_shader;
struct radeon_cmdbuf;
struct si_sqtt_fake_pipeline;
struct pipe_resource;
struct pipe_blit_info;
struct si_texture;
struct pipe_box;
struct pipe_context;

enum si_blitter_op /* bitmask */
{
   SI_SAVE_TEXTURES = 1,
   SI_SAVE_FRAMEBUFFER = 2,
   SI_SAVE_FRAGMENT_CONSTANT = 4,
   SI_DISABLE_RENDER_COND = 8,
};

/* si_blit.c */
void si_blitter_begin(struct si_context *sctx, enum si_blitter_op op);
void si_blitter_end(struct si_context *sctx);
void si_init_blit_functions(struct si_context *sctx);
void gfx6_decompress_textures(struct si_context *sctx, unsigned shader_mask);
void gfx11_decompress_textures(struct si_context *sctx, unsigned shader_mask);
MESAPROC void si_decompress_subresource(struct pipe_context *ctx, struct pipe_resource *tex, unsigned planes,
                                        unsigned level, unsigned first_layer, unsigned last_layer,
                                        bool need_fmask_expand) TAILV;
MESAPROC void si_resource_copy_region(struct pipe_context *ctx, struct pipe_resource *dst,
                                      unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                                      struct pipe_resource *src, unsigned src_level,
                                      const struct pipe_box *src_box) TAILV;
MESAPROC void si_gfx_copy_image(struct si_context *sctx, struct pipe_resource *dst,
                                unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                                struct pipe_resource *src, unsigned src_level,
                                const struct pipe_box *src_box) TAILV;
MESAPROC void si_decompress_dcc(struct si_context *sctx, struct si_texture *tex) TAILV;
void si_flush_implicit_resources(struct si_context *sctx);
MESAPROC void si_gfx_blit(struct pipe_context *ctx, const struct pipe_blit_info *info) TAILV;

/* si_nir_optim.c */
bool si_nir_is_output_const_if_tex_is_const(struct nir_shader *shader, float *in, float *out, int *texunit);

/* si_gfx_context.c */
MESAPROC bool si_init_gfx_context(struct si_screen *sscreen, struct si_context *sctx, unsigned flags) TAILB;
MESAPROC void si_fini_gfx_context(struct si_context *sctx) TAILV;
void si_destroy_llvm_compiler(struct ac_llvm_compiler *compiler);
void si_init_aux_async_compute_ctx(struct si_screen *sscreen);

/* si_gfx_screen.c */
MESAPROC bool si_init_gfx_screen(struct si_screen *sscreen) TAILBT;
MESAPROC void si_fini_gfx_screen(struct si_screen *sscreen) TAILV;

/* si_shader_cache.c */
void si_get_ir_cache_key(struct si_shader_selector *sel, bool ngg, bool es,
                         unsigned wave_size, unsigned char ir_blake3_cache_key[BLAKE3_KEY_LEN]);

bool si_init_shader_cache(struct si_screen *sscreen);

void si_init_screen_live_shader_cache(struct si_screen *sscreen);

void si_destroy_shader_cache(struct si_screen *sscreen);

bool si_shader_cache_load_shader(struct si_screen *sscreen, unsigned char ir_blake3_cache_key[BLAKE3_KEY_LEN],
                                 struct si_shader *shader);

void si_shader_cache_insert_shader(struct si_screen *sscreen, unsigned char ir_blake3_cache_key[BLAKE3_KEY_LEN],
                                   struct si_shader *shader, bool insert_into_disk_cache);

/* si_sqtt.c */
void si_sqtt_write_event_marker(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                                enum rgp_sqtt_marker_event_type api_type,
                                uint32_t vertex_offset_user_data,
                                uint32_t instance_offset_user_data,
                                uint32_t draw_index_user_data);
bool si_sqtt_register_pipeline(struct si_context* sctx, struct si_sqtt_fake_pipeline *pipeline,
                               uint32_t *gfx_sh_offsets);
bool si_sqtt_pipeline_is_registered(struct ac_sqtt *sqtt,
                                    uint64_t pipeline_hash);
void si_sqtt_describe_pipeline_bind(struct si_context* sctx, uint64_t pipeline_hash, int bind_point);
void
si_write_event_with_dims_marker(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                                enum rgp_sqtt_marker_event_type api_type,
                                uint32_t x, uint32_t y, uint32_t z);
void
si_write_user_event(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                    enum rgp_sqtt_marker_user_event_type type,
                    const char *str, int len);
MESAPROC void
si_sqtt_describe_barrier_start(struct si_context* sctx, struct radeon_cmdbuf *rcs) TAILV;
MESAPROC void
si_sqtt_describe_barrier_end(struct si_context* sctx, struct radeon_cmdbuf *rcs, unsigned flags) TAILV;
bool si_init_sqtt(struct si_context *sctx);
void si_destroy_sqtt(struct si_context *sctx);
MESAPROC void si_handle_sqtt(struct si_context *sctx, struct radeon_cmdbuf *rcs) TAILV;

/* si_mesh_shader.c */
void si_init_task_mesh_shader_functions(struct si_context *sctx);

/* si_nir_mediump.c */
void si_nir_lower_mediump_io_default(struct nir_shader *nir);
void si_nir_lower_mediump_io_option(struct nir_shader *nir);

static inline void si_need_gfx_cs_space(struct si_context *ctx, unsigned num_draws,
                                        unsigned extra_dw_per_draw)
{
   struct radeon_cmdbuf *cs = &ctx->gfx_cs;
   /* Don't count the needed CS space exactly and just use an upper bound.
    *
    * Also reserve space for stopping queries at the end of IB, because
    * the number of active queries is unlimited in theory.
    */
   unsigned reserve_dw = 2048 + ctx->num_cs_dw_queries_suspend +
      num_draws * (10 + extra_dw_per_draw);

   if (!ctx->ws->cs_check_space(cs, reserve_dw))
      si_flush_gfx_cs(ctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
}

static inline void si_select_draw_vbo(struct si_context *sctx)
{
   pipe_draw_func draw_vbo = sctx->draw_vbo[!!sctx->shader.tes.cso]
                                           [!!sctx->shader.gs.cso]
                                           [sctx->ngg];
   pipe_draw_vertex_state_func draw_vertex_state =
      sctx->draw_vertex_state[!!sctx->shader.tes.cso]
                             [!!sctx->shader.gs.cso]
                             [sctx->ngg];
   assert(draw_vbo);
   assert(draw_vertex_state);

   if (unlikely(sctx->real_draw_vbo)) {
      assert(sctx->real_draw_vertex_state);
      sctx->real_draw_vbo = draw_vbo;
      sctx->real_draw_vertex_state = draw_vertex_state;
   } else {
      assert(!sctx->real_draw_vertex_state);
      sctx->b.draw_vbo = draw_vbo;
      sctx->b.draw_vertex_state = draw_vertex_state;
   }
}

#ifdef __cplusplus
}
#endif

#endif /* SI_GFX_H */
