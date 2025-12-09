/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#define FD_BO_NO_HARDPIN 1

#include "fd6_barrier.h"
#include "fd6_const.h"
#include "fd6_compute.h"
#include "fd6_pack.h"
#include "fd6_screen.h"

#include "ir3_const.h"


static inline void
fd6_emit_driver_ubo(fd_cs &cs, const struct ir3_shader_variant *v,
                    int base, uint32_t sizedwords, unsigned buffer_offset,
                    struct fd_bo *bo)
{
   int size_vec4s = DIV_ROUND_UP(sizedwords, 4);

   /* base == ubo idx */
   fd_pkt7(cs, fd6_stage2opcode(v->type), 5)
      .add(CP_LOAD_STATE6_0(
         .dst_off = base,
         .state_type = ST6_UBO,
         .state_src = SS6_DIRECT,
         .state_block = fd6_stage2shadersb(v->type),
         .num_unit = 1,
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR())
      .add(A6XX_UBO_DESC(0, bo, buffer_offset, size_vec4s));
}

/* A helper to upload driver-params to a UBO, for the case where constants are
 * loaded by shader preamble rather than ST6_CONSTANTS
 */
static void
fd6_upload_emit_driver_ubo(struct fd_context *ctx, fd_cs &cs,
                           const struct ir3_shader_variant *v, int base,
                           uint32_t sizedwords, const void *dwords)
{
   struct pipe_context *pctx = &ctx->base;

   assert(FD_CALLX(ctx->screen->info, fd6_load_shader_consts_via_preamble)(v));

   if (!sizedwords || (base < 0))
      return;

   unsigned buffer_offset;
   struct pipe_resource *buffer = NULL;
   u_upload_data_ref(pctx->const_uploader, 0, sizedwords * sizeof(uint32_t),
                 16, dwords,  &buffer_offset, &buffer);
   if (!buffer)
      return;  /* nothing good will come of this.. */

   /* The backing BO may otherwise not be tracked by the resource, as
    * this allocation happens outside of the context of batch resource
    * tracking.
    */
   cs.attach_bo(fd_resource(buffer)->bo);

   fd6_emit_driver_ubo(cs, v, base, sizedwords, buffer_offset,
                       fd_resource(buffer)->bo);

   pipe_resource_reference(&buffer, NULL);
}

/* regid:          base const register
 * prsc or dwords: buffer containing constant values
 * sizedwords:     size of const value buffer
 */
void
fd6_emit_const_user(fd_cs &cs, const struct ir3_shader_variant *v, uint32_t regid,
                    uint32_t sizedwords, const uint32_t *dwords)
{
   emit_const_asserts(v, regid, sizedwords);

   /* NOTE we cheat a bit here, since we know mesa is aligning
    * the size of the user buffer to 16 bytes.  And we want to
    * cut cycles in a hot path.
    */
   uint32_t align_sz = align(sizedwords, 4);

   fd_pkt7(cs, fd6_stage2opcode(v->type), 3 + align_sz)
      .add(CP_LOAD_STATE6_0(
         .dst_off = regid / 4,
         .state_type = ST6_CONSTANTS,
         .state_src = SS6_DIRECT,
         .state_block = fd6_stage2shadersb(v->type),
         .num_unit = DIV_ROUND_UP(sizedwords, 4)
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR())
      .add(dwords, align_sz);
}

static void
emit_const_user(struct fd_ringbuffer *ring,
                const struct ir3_shader_variant *v, uint32_t regid,
                uint32_t size, const uint32_t *user_buffer)
{
   fd_cs cs(ring);
   fd6_emit_const_user(cs, v, regid, size, user_buffer);
}

void
fd6_emit_const_bo(fd_cs &cs, const struct ir3_shader_variant *v, uint32_t regid,
                  uint32_t offset, uint32_t sizedwords, struct fd_bo *bo)
{
   uint32_t dst_off = regid / 4;
   assert(dst_off % 4 == 0);
   uint32_t num_unit = DIV_ROUND_UP(sizedwords, 4);
   assert(num_unit % 4 == 0);

   emit_const_asserts(v, regid, sizedwords);

   fd_pkt7(cs, fd6_stage2opcode(v->type), 3)
      .add(CP_LOAD_STATE6_0(
         .dst_off = dst_off, .state_type = ST6_CONSTANTS,
         .state_src = SS6_INDIRECT,
         .state_block = fd6_stage2shadersb(v->type),
         .num_unit = num_unit,
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR(.bo = bo, .bo_offset = offset));
}

static void
emit_const_bo(struct fd_ringbuffer *ring,
              const struct ir3_shader_variant *v, uint32_t regid,
              uint32_t offset, uint32_t size, struct fd_bo *bo)
{
   fd_cs cs(ring);
   fd6_emit_const_bo(cs, v, regid, offset, size, bo);
}

static bool
is_stateobj(struct fd_ringbuffer *ring)
{
   return true;
}

static void
emit_const_ptrs(struct fd_ringbuffer *ring, const struct ir3_shader_variant *v,
                uint32_t dst_offset, uint32_t num, struct fd_bo **bos,
                uint32_t *offsets)
{
   UNREACHABLE("shouldn't be called on a6xx");
}

static void
wait_mem_writes(struct fd_context *ctx)
{
   ctx->batch->barrier |= FD6_WAIT_MEM_WRITES | FD6_INVALIDATE_CACHE | FD6_WAIT_FOR_IDLE;
}

template <chip CHIP>
static void
emit_stage_tess_consts(fd_cs &cs, const struct ir3_shader_variant *v,
                       struct fd_context *ctx, uint32_t *params, int num_params)
{
   const struct ir3_const_state *const_state = ir3_const_state(v);

   if (fd6_load_shader_consts_via_preamble<CHIP>(v)) {
      int base = const_state->primitive_param_ubo.idx;

      fd6_upload_emit_driver_ubo(ctx, cs, v, base, num_params, params);
   } else if (ir3_const_can_upload(&const_state->allocs,
                                   IR3_CONST_ALLOC_PRIMITIVE_PARAM,
                                   v->constlen)) {
      const unsigned regid =
         const_state->allocs.consts[IR3_CONST_ALLOC_PRIMITIVE_PARAM].offset_vec4;
      int size = MIN2(1 + regid, v->constlen) - regid;
      if (size > 0)
         fd6_emit_const_user(cs, v, regid * 4, num_params, params);
   }
}

template <chip CHIP>
struct fd_ringbuffer *
fd6_build_tess_consts(struct fd6_emit *emit)
{
   struct fd_context *ctx = emit->ctx;
   fd_cs constobj(ctx->batch->submit, 0x1000);

   /* VS sizes are in bytes since that's what STLW/LDLW use, while the HS
    * size is dwords, since that's what LDG/STG use.
    */
   unsigned num_vertices = emit->hs
                              ? ctx->patch_vertices
                              : emit->gs->gs.vertices_in;

   uint32_t vs_params[4] = {
      emit->vs->output_size * num_vertices * 4, /* vs primitive stride */
      emit->vs->output_size * 4,                /* vs vertex stride */
      0, 0};

   emit_stage_tess_consts<CHIP>(constobj, emit->vs, emit->ctx, vs_params, ARRAY_SIZE(vs_params));

   if (emit->hs) {
      struct fd_bo *tess_bo = ctx->screen->tess_bo;
      int64_t tess_factor_iova = fd_bo_get_iova(tess_bo);
      int64_t tess_param_iova = tess_factor_iova + FD6_TESS<CHIP>::FACTOR_SIZE;

      constobj.attach_bo(tess_bo);

      uint32_t hs_params[8] = {
         emit->vs->output_size * num_vertices * 4, /* vs primitive stride */
         emit->vs->output_size * 4,                /* vs vertex stride */
         emit->hs->output_size,
         ctx->patch_vertices,
         tess_param_iova,
         tess_param_iova >> 32,
         tess_factor_iova,
         tess_factor_iova >> 32,
      };

      emit_stage_tess_consts<CHIP>(constobj, emit->hs, emit->ctx,
                                   hs_params, ARRAY_SIZE(hs_params));

      if (emit->gs)
         num_vertices = emit->gs->gs.vertices_in;

      uint32_t ds_params[8] = {
         emit->ds->output_size * num_vertices * 4, /* ds primitive stride */
         emit->ds->output_size * 4,                /* ds vertex stride */
         emit->hs->output_size,                    /* hs vertex stride (dwords) */
         emit->hs->tess.tcs_vertices_out,
         tess_param_iova,
         tess_param_iova >> 32,
         tess_factor_iova,
         tess_factor_iova >> 32,
      };

      emit_stage_tess_consts<CHIP>(constobj, emit->ds, emit->ctx,
                                   ds_params,  ARRAY_SIZE(ds_params));
   }

   if (emit->gs) {
      const struct ir3_shader_variant *prev;
      if (emit->ds)
         prev = emit->ds;
      else
         prev = emit->vs;

      uint32_t gs_params[4] = {
         prev->output_size * num_vertices * 4, /* ds primitive stride */
         prev->output_size * 4,                /* ds vertex stride */
         0,
         0,
      };

      num_vertices = emit->gs->gs.vertices_in;
      emit_stage_tess_consts<CHIP>(constobj, emit->gs, emit->ctx,
                                   gs_params, ARRAY_SIZE(gs_params));
   }

   return constobj;
}
FD_GENX(fd6_build_tess_consts);

static void
fd6_emit_ubos(const struct ir3_shader_variant *v, fd_cs &cs,
              struct fd_constbuf_stateobj *constbuf)
{
   const struct ir3_const_state *const_state = ir3_const_state(v);
   int num_ubos = const_state->num_app_ubos;

   if (!num_ubos)
      return;

   fd_pkt7 pkt(cs, fd6_stage2opcode(v->type), 3 + (2 * num_ubos));

   pkt.add(CP_LOAD_STATE6_0(
         .dst_off = 0,
         .state_type = ST6_UBO,
         .state_src = SS6_DIRECT,
         .state_block = fd6_stage2shadersb(v->type),
         .num_unit = num_ubos,
      ))
      .add(CP_LOAD_STATE6_EXT_SRC_ADDR());

   for (int i = 0; i < num_ubos; i++) {
      struct pipe_constant_buffer *cb = &constbuf->cb[i];

      if (cb->buffer) {
         struct fd_bo *bo = fd_resource(cb->buffer)->bo;
         int size_vec4s = DIV_ROUND_UP(cb->buffer_size, 16);
         pkt.add(A6XX_UBO_DESC(i, bo, cb->buffer_offset, size_vec4s));
      } else {
         pkt.add(A6XX_UBO_DESC(i, NULL, 0, 0));
      }
   }
}

template <chip CHIP>
unsigned
fd6_user_consts_cmdstream_size(const struct ir3_shader_variant *v)
{
   if (!v)
      return 0;

   const struct ir3_const_state *const_state = ir3_const_state(v);
   const struct ir3_ubo_analysis_state *ubo_state = &const_state->ubo_state;
   unsigned packets, size;

   if (fd6_load_shader_consts_via_preamble<CHIP>(v)) {
      packets = 0;
      size = 0;
   } else {
      /* pre-calculate size required for userconst stateobj: */
      ir3_user_consts_size(ubo_state, &packets, &size);
   }

   /* also account for UBO addresses: */
   packets += 1;
   size += 2 * const_state->num_app_ubos;

   unsigned sizedwords = (4 * packets) + size;
   return sizedwords * 4;
}
FD_GENX(fd6_user_consts_cmdstream_size);

template <chip CHIP>
static void
emit_user_consts(const struct ir3_shader_variant *v, fd_cs &cs,
                 struct fd_constbuf_stateobj *constbuf)
{
   fd6_emit_ubos(v, cs, constbuf);

   if (fd6_load_shader_consts_via_preamble<CHIP>(v))
      return;

   ir3_emit_user_consts(v, cs, constbuf);
}

template <fd6_pipeline_type PIPELINE, chip CHIP>
struct fd_ringbuffer *
fd6_build_user_consts(struct fd6_emit *emit)
{
   struct fd_context *ctx = emit->ctx;
   unsigned sz = emit->prog->user_consts_cmdstream_size;

   fd_cs constobj(ctx->batch->submit, sz);

   emit_user_consts<CHIP>(emit->vs, constobj, &ctx->constbuf[MESA_SHADER_VERTEX]);

   if (PIPELINE == HAS_TESS_GS) {
      if (emit->hs) {
         emit_user_consts<CHIP>(emit->hs, constobj, &ctx->constbuf[MESA_SHADER_TESS_CTRL]);
         emit_user_consts<CHIP>(emit->ds, constobj, &ctx->constbuf[MESA_SHADER_TESS_EVAL]);
      }
      if (emit->gs) {
         emit_user_consts<CHIP>(emit->gs, constobj, &ctx->constbuf[MESA_SHADER_GEOMETRY]);
      }
   }
   emit_user_consts<CHIP>(emit->fs, constobj, &ctx->constbuf[MESA_SHADER_FRAGMENT]);

   return constobj;
}
FD_GENX2(fd6_build_user_consts, fd6_pipeline_type, NO_TESS_GS);
FD_GENX2(fd6_build_user_consts, fd6_pipeline_type, HAS_TESS_GS);

template <chip CHIP>
static inline void
emit_driver_params(const struct ir3_shader_variant *v, fd_cs &dpconstobj,
                   struct fd_context *ctx, const struct pipe_draw_info *info,
                   const struct pipe_draw_indirect_info *indirect,
                   const struct ir3_driver_params_vs *vertex_params)
{
   if (fd6_load_shader_consts_via_preamble<CHIP>(v)) {
      const struct ir3_const_state *const_state = ir3_const_state(v);
      int base = const_state->driver_params_ubo.idx;

      fd6_upload_emit_driver_ubo(ctx, dpconstobj, v, base,
                                 dword_sizeof(*vertex_params),
                                 vertex_params);
   } else {
      ir3_emit_driver_params(v, dpconstobj, ctx, info, indirect, vertex_params);
   }
}

template <chip CHIP>
static inline void
emit_hs_driver_params(const struct ir3_shader_variant *v, fd_cs &dpconstobj,
                      struct fd_context *ctx)
{
   if (fd6_load_shader_consts_via_preamble<CHIP>(v)) {
      const struct ir3_const_state *const_state = ir3_const_state(v);
      struct ir3_driver_params_tcs hs_params = ir3_build_driver_params_tcs(ctx);
      int base = const_state->driver_params_ubo.idx;

      fd6_upload_emit_driver_ubo(ctx, dpconstobj, v, base,
                                 dword_sizeof(hs_params),
                                 &hs_params);
   } else {
      ir3_emit_hs_driver_params(v, dpconstobj, ctx);
   }
}

template <fd6_pipeline_type PIPELINE, chip CHIP>
struct fd_ringbuffer *
fd6_build_driver_params(struct fd6_emit *emit)
{
   struct fd_context *ctx = emit->ctx;
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   unsigned num_dp = emit->prog->num_driver_params;
   unsigned num_ubo_dp;

   if (CHIP == A6XX) {
      assert(!emit->prog->num_ubo_driver_params);
      /* Make it easier for compiler to see that this path isn't used on a6xx: */
      num_ubo_dp = 0;
   } else {
      num_ubo_dp = emit->prog->num_ubo_driver_params;
   }

   if (!num_dp && !num_ubo_dp) {
      fd6_ctx->has_dp_state = false;
      return NULL;
   }

   bool needs_ucp = !!emit->vs->key.ucp_enables;

   if (PIPELINE == HAS_TESS_GS) {
      needs_ucp |= emit->gs && emit->gs->key.ucp_enables;
      needs_ucp |= emit->hs && emit->hs->key.ucp_enables;
      needs_ucp |= emit->ds && emit->ds->key.ucp_enables;
   }

   struct ir3_driver_params_vs p =
      ir3_build_driver_params_vs(ctx, emit->info, emit->draw, emit->draw_id, needs_ucp);

   unsigned size_dwords =
      num_dp * (4 + dword_sizeof(p)) + /* 4dw PKT7 header */
      num_ubo_dp * 6;                  /* 6dw per UBO descriptor */

   fd_cs dpconstobj(ctx->batch->submit, size_dwords * 4);

   /* VS still works the old way*/
   if (emit->vs->need_driver_params) {
      ir3_emit_driver_params(emit->vs, dpconstobj, ctx, emit->info, emit->indirect, &p);
   }

   if (PIPELINE == HAS_TESS_GS) {
      if (emit->gs && emit->gs->need_driver_params) {
         emit_driver_params<CHIP>(emit->gs, dpconstobj, ctx, emit->info, emit->indirect, &p);
      }

      if (emit->hs && emit->hs->need_driver_params) {
         emit_hs_driver_params<CHIP>(emit->hs, dpconstobj, ctx);
      }

      if (emit->ds && emit->ds->need_driver_params) {
         emit_driver_params<CHIP>(emit->ds, dpconstobj, ctx, emit->info, emit->indirect, &p);
      }
   }

   if (emit->indirect)
      wait_mem_writes(ctx);

   fd6_ctx->has_dp_state = true;

   return dpconstobj;
}
FD_GENX2(fd6_build_driver_params, fd6_pipeline_type, NO_TESS_GS);
FD_GENX2(fd6_build_driver_params, fd6_pipeline_type, HAS_TESS_GS);

template <chip CHIP>
void
fd6_emit_cs_driver_params(struct fd_context *ctx, fd_cs &cs,
                          const struct ir3_shader_variant *v,
                          const struct pipe_grid_info *info)
{
   if (fd6_load_shader_consts_via_preamble<CHIP>(v)) {
      const struct ir3_const_state *const_state = ir3_const_state(v);
      struct ir3_driver_params_cs compute_params =
         ir3_build_driver_params_cs(v, info);
      int base = const_state->driver_params_ubo.idx;

      if (base < 0)
         return;

      struct pipe_resource *buffer = NULL;
      unsigned buffer_offset;

      u_upload_data_ref(ctx->base.const_uploader, 0, sizeof(compute_params),
                     16, &compute_params,  &buffer_offset, &buffer);

      if (info->indirect) {
         /* Copy indirect params into UBO: */
         ctx->screen->mem_to_mem(cs, buffer, buffer_offset, info->indirect,
                                 info->indirect_offset, 3);

         wait_mem_writes(ctx);
      } else {
         cs.attach_bo(fd_resource(buffer)->bo);
      }

      fd6_emit_driver_ubo(cs, v, base, dword_sizeof(compute_params),
                          buffer_offset, fd_resource(buffer)->bo);

      pipe_resource_reference(&buffer, NULL);
   } else {
      ir3_emit_cs_driver_params(v, cs, ctx, info);
      if (info->indirect)
         wait_mem_writes(ctx);
   }
}
FD_GENX(fd6_emit_cs_driver_params);

template <chip CHIP>
void
fd6_emit_cs_user_consts(struct fd_context *ctx, fd_cs &cs,
                        const struct ir3_shader_variant *v)
{
   emit_user_consts<CHIP>(v, cs, &ctx->constbuf[MESA_SHADER_COMPUTE]);
}
FD_GENX(fd6_emit_cs_user_consts);

template <chip CHIP>
void
fd6_emit_immediates(const struct ir3_shader_variant *v, fd_cs &cs)
{
   const struct ir3_const_state *const_state = ir3_const_state(v);

   if (const_state->consts_ubo.idx >= 0) {
      int sizedwords = DIV_ROUND_UP(v->constant_data_size, 4);

      fd6_emit_driver_ubo(cs, v, const_state->consts_ubo.idx, sizedwords,
                          v->info.constant_data_offset, v->bo);
   }

   if (fd6_load_inline_uniforms_via_preamble_ldgk<CHIP>(v))
      return;

   ir3_emit_immediates(v, cs);
}
FD_GENX(fd6_emit_immediates);

template <chip CHIP>
void
fd6_emit_link_map(struct fd_context *ctx, fd_cs &cs,
                  const struct ir3_shader_variant *producer,
                  const struct ir3_shader_variant *consumer)
{
   if (fd6_load_inline_uniforms_via_preamble_ldgk<CHIP>(producer)) {
      const struct ir3_const_state *const_state = ir3_const_state(consumer);
      int base = const_state->primitive_map_ubo.idx;
      uint32_t size = align(consumer->input_size, 4);

      fd6_upload_emit_driver_ubo(ctx, cs, consumer, base, size, producer->output_loc);
   } else {
      ir3_emit_link_map(producer, consumer, cs);
   }
}
FD_GENX(fd6_emit_link_map);
