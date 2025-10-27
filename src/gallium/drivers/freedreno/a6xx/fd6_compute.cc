/*
 * Copyright © 2019 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "drm/freedreno_ringbuffer.h"
#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/u_dump.h"
#include "u_tracepoints.h"

#include "freedreno_resource.h"
#include "freedreno_tracepoints.h"

#include "fd6_barrier.h"
#include "fd6_compute.h"
#include "fd6_const.h"
#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_pack.h"

/* nregs: 2 */
template <chip CHIP>
static void
cs_program_emit_local_size(struct fd_context *ctx, fd_crb &crb,
                           struct ir3_shader_variant *v, uint16_t local_size[3])
{
   /*
    * Devices that do not support double threadsize take the threadsize from
    * A6XX_SP_PS_WAVE_CNTL_THREADSIZE instead of A6XX_SP_CS_WGE_CNTL_THREADSIZE
    * which is always set to THREAD128.
    */
   enum a6xx_threadsize thrsz = v->info.double_threadsize ? THREAD128 : THREAD64;
   enum a6xx_threadsize thrsz_cs = ctx->screen->info->props
      .supports_double_threadsize ? thrsz : THREAD128;

   if (CHIP >= A7XX) {
      unsigned tile_height = (local_size[1] % 8 == 0)   ? 3
                             : (local_size[1] % 4 == 0) ? 5
                             : (local_size[1] % 2 == 0) ? 9
                                                           : 17;

      crb.add(SP_CS_WGE_CNTL(CHIP,
         .linearlocalidregid = INVALID_REG,
         .threadsize = thrsz_cs,
         .workgrouprastorderzfirsten = true,
         .wgtilewidth = 4,
         .wgtileheight = tile_height,
      ));

      crb.add(SP_CS_NDRANGE_7(CHIP,
         .localsizex = local_size[0] - 1,
         .localsizey = local_size[1] - 1,
         .localsizez = local_size[2] - 1,
      ));
   }
}

/* nregs: 9 */
template <chip CHIP>
static void
cs_program_emit(struct fd_context *ctx, fd_crb &crb, struct ir3_shader_variant *v)
   assert_dt
{
   crb.add(SP_UPDATE_CNTL(CHIP,
      .vs_state = true, .hs_state = true,
      .ds_state = true, .gs_state = true,
      .fs_state = true, .cs_state = true,
      .cs_uav = true, .gfx_uav = true,
   ));

   crb.add(SP_CS_CONST_CONFIG(CHIP,
      .constlen = v->constlen,
      .enabled = true,
   ));

   crb.add(A6XX_SP_CS_CONFIG(
      .bindless_tex = v->bindless_tex,
      .bindless_samp = v->bindless_samp,
      .bindless_uav = v->bindless_ibo,
      .bindless_ubo = v->bindless_ubo,
      .enabled = true,
      .ntex = v->num_samp,
      .nsamp = v->num_samp,
      .nuav = ir3_shader_num_uavs(v),
   ));

   uint32_t local_invocation_id = v->cs.local_invocation_id;
   uint32_t work_group_id = v->cs.work_group_id;

   /*
    * Devices that do not support double threadsize take the threadsize from
    * A6XX_SP_PS_WAVE_CNTL_THREADSIZE instead of A6XX_SP_CS_WGE_CNTL_THREADSIZE
    * which is always set to THREAD128.
    */
   enum a6xx_threadsize thrsz = v->info.double_threadsize ? THREAD128 : THREAD64;
   enum a6xx_threadsize thrsz_cs = ctx->screen->info->props
      .supports_double_threadsize ? thrsz : THREAD128;

   if (CHIP == A6XX) {
      crb.add(SP_CS_CONST_CONFIG_0(CHIP,
         .wgidconstid = work_group_id,
         .wgsizeconstid = INVALID_REG,
         .wgoffsetconstid = INVALID_REG,
         .localidregid = local_invocation_id,
      ));
      crb.add(SP_CS_WGE_CNTL(CHIP,
         .linearlocalidregid = INVALID_REG,
         .threadsize = thrsz_cs,
      ));

      if (!ctx->screen->info->props.supports_double_threadsize) {
         crb.add(SP_PS_WAVE_CNTL(CHIP, .threadsize = thrsz));
      }

      if (ctx->screen->info->props.has_lpac) {
         crb.add(A6XX_SP_CS_WIE_CNTL_0(
            .wgidconstid = work_group_id,
            .wgsizeconstid = INVALID_REG,
            .wgoffsetconstid = INVALID_REG,
            .localidregid = local_invocation_id,
         ));
         crb.add(SP_CS_WIE_CNTL_1(CHIP,
            .linearlocalidregid = INVALID_REG,
            .threadsize = thrsz,
         ));
      }
   } else {
      crb.add(SP_PS_WAVE_CNTL(CHIP, .threadsize = THREAD64));
      crb.add(A6XX_SP_CS_WIE_CNTL_0(
         .wgidconstid = work_group_id,
         .wgsizeconstid = INVALID_REG,
         .wgoffsetconstid = INVALID_REG,
         .localidregid = local_invocation_id,
      ));
      crb.add(SP_CS_WIE_CNTL_1(CHIP,
         .linearlocalidregid = INVALID_REG,
         .threadsize = thrsz_cs,
         .workitemrastorder =
            v->cs.force_linear_dispatch ? WORKITEMRASTORDER_LINEAR
                                          : WORKITEMRASTORDER_TILED,
      ));
      crb.add(SP_CS_HYSTERESIS(CHIP, 0)); // Sometimes is 0x08000000
   }

   if (!v->local_size_variable)
      cs_program_emit_local_size<CHIP>(ctx, crb, v, v->local_size);
}

template <chip CHIP>
static void
fd6_launch_grid(struct fd_context *ctx, const struct pipe_grid_info *info) in_dt
{
   struct fd6_compute_state *cp = (struct fd6_compute_state *)ctx->compute;

   if (unlikely(!cp->v)) {
      struct ir3_shader_state *hwcso = (struct ir3_shader_state *)cp->hwcso;
      struct ir3_shader_key key = {};

      cp->v = ir3_shader_variant(ir3_get_shader(hwcso), key, false, &ctx->debug);
      if (!cp->v)
         return;

      cp->stateobj = fd_ringbuffer_new_object(ctx->pipe, 0x1000);
      fd_cs cs(cp->stateobj);
      with_crb (cs, 9)
         cs_program_emit<CHIP>(ctx, crb, cp->v);
      fd6_emit_shader<CHIP>(ctx, cs, cp->v);
   }

   fd_cs cs(ctx->batch->draw);

   fd6_set_render_mode<CHIP>(cs, {RM6_COMPUTE});

   trace_start_compute(&ctx->batch->trace, cs, !!info->indirect, info->work_dim,
                       info->block[0], info->block[1], info->block[2],
                       info->grid[0],  info->grid[1],  info->grid[2],
                       cp->v->shader_id);

   fd6_barrier_flush<CHIP>(cs, ctx->batch);

   bool emit_instrlen_workaround =
      cp->v->instrlen > ctx->screen->info->props.instr_cache_size;

   /* There appears to be a HW bug where in some rare circumstances it appears
    * to accidentally use the FS instrlen instead of the CS instrlen, which
    * affects all known gens. Based on various experiments it appears that the
    * issue is that when prefetching a branch destination and there is a cache
    * miss, when fetching from memory the HW bounds-checks the fetch against
    * SP_CS_INSTR_SIZE, except when one of the two register contexts is active
    * it accidentally fetches SP_PS_INSTR_SIZE from the other (inactive)
    * context. To workaround it we set the FS instrlen here and do a dummy
    * event to roll the context (because it fetches SP_PS_INSTR_SIZE from the
    * "wrong" context). Because the bug seems to involve cache misses, we
    * don't emit this if the entire CS program fits in cache, which will
    * hopefully be the majority of cases.
    *
    * See https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/19023
    */
   if (emit_instrlen_workaround) {
      fd_pkt4(cs, 1)
         .add(A6XX_SP_PS_INSTR_SIZE(cp->v->instrlen));
      fd6_event_write<CHIP>(ctx, cs, FD_LABEL);
   }

   if (ctx->gen_dirty)
      fd6_emit_cs_state<CHIP>(ctx, cs, cp);

   if (ctx->gen_dirty & BIT(FD6_GROUP_CONST))
      fd6_emit_cs_user_consts<CHIP>(ctx, cs, cp->v);

   if (cp->v->need_driver_params)
      fd6_emit_cs_driver_params<CHIP>(ctx, cs, cp->v, info);

   const unsigned *local_size =
      info->block; // v->shader->nir->info->workgroup_size;
   const unsigned *num_groups = info->grid;
   /* for some reason, mesa/st doesn't set info->work_dim, so just assume 3: */
   const unsigned work_dim = info->work_dim ? info->work_dim : 3;

   with_crb (cs, 15) {
      uint32_t shared_size =
         MAX2(((int)(cp->v->cs.req_local_mem + info->variable_shared_mem) - 1) / 1024, 1);
      enum a6xx_const_ram_mode mode =
         cp->v->constlen > 256 ? CONSTLEN_512 :
         (cp->v->constlen > 192 ? CONSTLEN_256 :
         (cp->v->constlen > 128 ? CONSTLEN_192 : CONSTLEN_128));
      crb.add(A6XX_SP_CS_CNTL_1(
         .shared_size = shared_size,
         .constantrammode = mode,
      ));

      if (CHIP == A6XX && ctx->screen->info->props.has_lpac) {
         crb.add(HLSQ_CS_CTRL_REG1(CHIP,
            .shared_size = shared_size,
            .constantrammode = mode,
         ));
      }

      if (cp->v->local_size_variable) {
         uint16_t wg[] = {local_size[0], local_size[1], local_size[2]};
         cs_program_emit_local_size<CHIP>(ctx, crb, cp->v, wg);
      }

      crb.add(SP_CS_NDRANGE_0(CHIP,
         .kerneldim = work_dim,
         .localsizex = local_size[0] - 1,
         .localsizey = local_size[1] - 1,
         .localsizez = local_size[2] - 1,
      ));
      crb.add(SP_CS_NDRANGE_1(CHIP,
         .globalsize_x = local_size[0] * num_groups[0],
      ));
      crb.add(SP_CS_NDRANGE_2(CHIP, .globaloff_x = 0));
      crb.add(SP_CS_NDRANGE_3(CHIP,
         .globalsize_y = local_size[1] * num_groups[1],
      ));
      crb.add(SP_CS_NDRANGE_4(CHIP, .globaloff_y = 0));
      crb.add(SP_CS_NDRANGE_5(CHIP,
         .globalsize_z = local_size[2] * num_groups[2],
      ));
      crb.add(SP_CS_NDRANGE_6(CHIP, .globaloff_z = 0));

      crb.add(SP_CS_KERNEL_GROUP_X(CHIP, 1));
      crb.add(SP_CS_KERNEL_GROUP_Y(CHIP, 1));
      crb.add(SP_CS_KERNEL_GROUP_Z(CHIP, 1));
   }

   if (info->indirect) {
      struct fd_resource *rsc = fd_resource(info->indirect);

      fd_pkt7(cs, CP_EXEC_CS_INDIRECT, 4)
         .add(A4XX_CP_EXEC_CS_INDIRECT_0())
         .add(A5XX_CP_EXEC_CS_INDIRECT_ADDR(rsc->bo, info->indirect_offset))
         .add(A5XX_CP_EXEC_CS_INDIRECT_3(
            .localsizex = local_size[0] - 1,
            .localsizey = local_size[1] - 1,
            .localsizez = local_size[2] - 1,
         ));
   } else {
      fd_pkt7(cs, CP_EXEC_CS, 4)
         .add(CP_EXEC_CS_0())
         .add(CP_EXEC_CS_1(info->grid[0]))
         .add(CP_EXEC_CS_2(info->grid[1]))
         .add(CP_EXEC_CS_3(info->grid[2]));
   }

   trace_end_compute(&ctx->batch->trace, cs);

   fd_context_all_clean(ctx);
}

static void *
fd6_compute_state_create(struct pipe_context *pctx,
                         const struct pipe_compute_state *cso)
{
   struct fd6_compute_state *hwcso =
         (struct fd6_compute_state *)calloc(1, sizeof(*hwcso));
   hwcso->hwcso = ir3_shader_compute_state_create(pctx, cso);
   return hwcso;
}

static void
fd6_compute_state_delete(struct pipe_context *pctx, void *_hwcso)
{
   struct fd6_compute_state *hwcso = (struct fd6_compute_state *)_hwcso;
   ir3_shader_state_delete(pctx, hwcso->hwcso);
   if (hwcso->stateobj)
      fd_ringbuffer_del(hwcso->stateobj);
   free(hwcso);
}

static void
fd6_get_compute_state_info(struct pipe_context *pctx, void *cso, struct pipe_compute_state_object_info *cinfo)
{
   static struct ir3_shader_key key; /* static is implicitly zeroed */
   struct fd6_compute_state *cs = (struct fd6_compute_state *)cso;
   struct ir3_shader_state *hwcso = (struct ir3_shader_state *)cs->hwcso;
   struct ir3_shader_variant *v = ir3_shader_variant(ir3_get_shader(hwcso), key, false, &pctx->debug);
   const struct fd_dev_info *info = fd_context(pctx)->screen->info;
   uint32_t threadsize_base = info->threadsize_base;

   cinfo->max_threads = threadsize_base * info->max_waves;
   cinfo->simd_sizes = threadsize_base;
   cinfo->preferred_simd_size = threadsize_base;

   if (info->props.supports_double_threadsize && v->info.double_threadsize) {

      cinfo->max_threads *= 2;
      cinfo->simd_sizes |= (threadsize_base * 2);
      cinfo->preferred_simd_size *= 2;
   }

   unsigned reg_file_size_vec4 = info->props.reg_size_vec4 * threadsize_base * info->wave_granularity;
   unsigned vec4_regs_per_thread = MAX2(v->info.max_reg + 1, 1);

   cinfo->max_threads = MIN2(cinfo->max_threads, reg_file_size_vec4 / vec4_regs_per_thread);

   cinfo->private_memory = v->pvtmem_size;
}

template <chip CHIP>
void
fd6_compute_init(struct pipe_context *pctx)
   disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->launch_grid = fd6_launch_grid<CHIP>;
   pctx->create_compute_state = fd6_compute_state_create;
   pctx->delete_compute_state = fd6_compute_state_delete;
   pctx->get_compute_state_info = fd6_get_compute_state_info;
}
FD_GENX(fd6_compute_init);
