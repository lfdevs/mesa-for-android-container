/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "gfx/si_gfx.h"
#include "mm/si_mm.h"
#include "tests/si_tests.h"

#include "driver_ddebug/dd_util.h"
#include "si_public.h"
#include "util/u_memory.h"
#include "util/u_suballoc.h"
#include "util/u_upload_mgr.h"
#include "util/xmlconfig.h"

#if AMD_LLVM_AVAILABLE
#include "ac_llvm_util.h"
#endif

#if HAVE_AMDGPU_VIRTIO
#include "virtio/virtio-gpu/drm_hw.h"
#endif

#include <xf86drm.h>

static const struct debug_named_value radeonsi_debug_options[] = {
   /* Information logging options: */
   {"info", DBG(INFO), "Print driver information"},
   {"tex", DBG(TEX), "Print texture info"},
   {"compute", DBG(COMPUTE), "Print compute info"},
   {"vm", DBG(VM), "Print virtual addresses when creating resources"},
   {"cache_stats", DBG(CACHE_STATS), "Print shader cache statistics."},
   {"ib", DBG(IB), "Print command buffers."},
   {"elements", DBG(VERTEX_ELEMENTS), "Print vertex elements."},

   /* Driver options: */
   {"nowc", DBG(NO_WC), "Disable GTT write combining"},
   {"nowcstream", DBG(NO_WC_STREAM), "Disable GTT write combining for streaming uploads"},
   {"check_vm", DBG(CHECK_VM), "Check VM faults and dump debug info."},
   {"reserve_vmid", DBG(RESERVE_VMID), "Force VMID reservation per context."},
   {"shadowregs", DBG(SHADOW_REGS), "Enable CP register shadowing."},
   {"userqnoshadowregs", DBG(USERQ_NO_SHADOW_REGS), "Disable register shadowing in userqueue."},
   {"nofastdlist", DBG(NO_FAST_DISPLAY_LIST), "Disable fast display lists"},
   {"nodmashaders", DBG(NO_DMA_SHADERS), "Disable uploading shaders via CP DMA and map them directly."},

   /* 3D engine options: */
   {"nongg", DBG(NO_NGG), "Disable NGG and use the legacy pipeline."},
   {"nggc", DBG(ALWAYS_NGG_CULLING_ALL), "Always use NGG culling even when it can hurt."},
   {"nonggc", DBG(NO_NGG_CULLING), "Disable NGG culling."},
   {"switch_on_eop", DBG(SWITCH_ON_EOP), "Program WD/IA to switch on end-of-packet."},
   {"nooutoforder", DBG(NO_OUT_OF_ORDER), "Disable out-of-order rasterization"},
   {"nodpbb", DBG(NO_DPBB), "Disable DPBB. Overrules the dpbb enable option."},
   {"dpbb", DBG(DPBB), "Enable DPBB for gfx9 dGPU. Default enabled for gfx9 APU and >= gfx10."},
   {"nohyperz", DBG(NO_HYPERZ), "Disable Hyper-Z"},
   {"no2d", DBG(NO_2D_TILING), "Disable 2D tiling"},
   {"notiling", DBG(NO_TILING), "Disable tiling"},
   {"nodisplaytiling", DBG(NO_DISPLAY_TILING), "Disable display tiling"},
   {"nodisplaydcc", DBG(NO_DISPLAY_DCC), "Disable display DCC"},
   {"noexporteddcc", DBG(NO_EXPORTED_DCC), "Disable DCC for all exported buffers (via DMABUF, etc.)"},
   {"nodcc", DBG(NO_DCC), "Disable DCC."},
   {"nodccclear", DBG(NO_DCC_CLEAR), "Disable DCC fast clear."},
   {"nodccstore", DBG(NO_DCC_STORE), "Disable DCC stores"},
   {"dccstore", DBG(DCC_STORE), "Enable DCC stores"},
   {"nodccmsaa", DBG(NO_DCC_MSAA), "Disable DCC for MSAA"},
   {"nofmask", DBG(NO_FMASK), "Disable MSAA compression"},
   {"nodma", DBG(NO_DMA), "Disable SDMA-copy for DRI_PRIME"},

   {"forcegfxblit", DBG(FORCE_GFX_BLIT), "Force the use of fragment shaders for image clears, copies, blits, and resolve."},
   {"forcecomputeblit", DBG(FORCE_COMPUTE_BLIT), "Force the use of compute shaders for image clears, copies, blits, and resolve."},
   {"forcefastclear", DBG(FORCE_FAST_CLEAR), "Force the use of image \"fast clear\" when possible. For debug only."},

   {"extra_md", DBG(EXTRA_METADATA), "Set UMD metadata for all textures and with additional fields for umr"},

   {"tmz", DBG(TMZ), "Force allocation of scanout/depth/stencil buffer as encrypted"},
   {"sqtt", DBG(SQTT), "Enable SQTT"},
   {"export_modifier", DBG(EXPORT_MODIFIER), "Export real modifier instead of DRM_FORMAT_MOD_INVALID"},

   DEBUG_NAMED_VALUE_END /* must be last */
};

bool si_virtgpu_probe_nctx(int fd, const struct virgl_renderer_capset_drm *caps)
{
   #ifdef HAVE_AMDGPU_VIRTIO
   return caps->context_type == VIRTGPU_DRM_CONTEXT_AMDGPU;
   #else
   return false;
   #endif
}

static bool si_is_resource_busy(struct pipe_screen *screen, struct pipe_resource *resource,
                                unsigned usage)
{
   struct radeon_winsys *ws = ((struct si_screen *)screen)->ws;

   return !ws->buffer_wait(ws, si_resource(resource)->buf, 0,
                           /* If mapping for write, we need to wait for all reads and writes.
                            * If mapping for read, we only need to wait for writes.
                            */
                           (usage & PIPE_MAP_WRITE ? RADEON_USAGE_READWRITE : RADEON_USAGE_WRITE) |
                           RADEON_USAGE_DISALLOW_SLOW_REPLY);
}

static struct pipe_context *si_pipe_create_context(struct pipe_screen *screen, void *priv,
                                                   unsigned flags)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct pipe_context *ctx;
   struct si_context *sctx;

   if (sscreen->debug_flags & DBG(CHECK_VM))
      flags |= PIPE_CONTEXT_DEBUG;

   ctx = si_create_context(screen, flags);
   sctx = (struct si_context *)ctx;

   if (!(flags & PIPE_CONTEXT_PREFER_THREADED))
      return ctx;

   /* Clover (compute-only) is unsupported. */
   if (flags & PIPE_CONTEXT_COMPUTE_ONLY)
      return ctx;

   /* When shaders are logged to stderr, asynchronous compilation is
    * disabled too. */
   if (sscreen->shader_debug_flags & DBG_ALL_SHADERS)
      return ctx;

   /* Use asynchronous flushes only on amdgpu, since the radeon
    * implementation for fence_server_sync is incomplete. */
   struct pipe_context *tc =
      threaded_context_create(ctx, &sscreen->pool_transfers,
                              si_replace_buffer_storage,
                              &(struct threaded_context_options){
                                 .create_fence = sscreen->info.is_amdgpu ?
                                       si_create_fence : NULL,
                                 .is_resource_busy = si_is_resource_busy,
                                 .driver_calls_flush_notify = true,
                                 .unsynchronized_create_fence_fd = true,
                              },
                              &sctx->tc);

   if (tc && tc != ctx)
      threaded_context_init_bytes_mapped_limit((struct threaded_context *)tc, 4);

   return tc;
}

/*
 * pipe_screen
 */
void si_destroy_screen(struct pipe_screen *pscreen)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   if (!sscreen->ws->unref(sscreen->ws))
      return;

   for (unsigned i = 0; i < ARRAY_SIZE(sscreen->aux_contexts); i++) {
      mtx_lock(&sscreen->aux_contexts[i].lock);

      if (sscreen->aux_contexts[i].ctx) {
         struct si_context *saux = (struct si_context*)sscreen->aux_contexts[i].ctx;
         struct u_log_context *aux_log = saux->log;
         if (aux_log) {
            saux->b.set_log_context(&saux->b, NULL);
            u_log_context_destroy(aux_log);
            FREE(aux_log);
         }

         saux->b.destroy(&saux->b);
      }

      mtx_unlock(&sscreen->aux_contexts[i].lock);
      mtx_destroy(&sscreen->aux_contexts[i].lock);
   }

   si_fini_gfx_screen(sscreen);

   simple_mtx_destroy(&sscreen->print_ib_mutex);

   slab_destroy_parent(&sscreen->pool_transfers);

   util_idalloc_mt_fini(&sscreen->buffer_ids);

   sscreen->ws->destroy(sscreen->ws);
   FREE(sscreen);
}

static struct pipe_screen *radeonsi_screen_create_impl(struct radeon_winsys *ws,
                                                       const struct pipe_screen_config *config)
{
   struct si_screen *sscreen = CALLOC_STRUCT(si_screen);

   if (!sscreen) {
      return NULL;
   }

   {
#define OPT_BOOL(name, dflt, description)                                                          \
   sscreen->options.name = driQueryOptionb(config->options, "radeonsi_" #name);
#define OPT_INT(name, dflt, description)                                                           \
   sscreen->options.name = driQueryOptioni(config->options, "radeonsi_" #name);
#include "si_debug_options.h"
   }

   sscreen->ws = ws;
   ws->query_info(ws, &sscreen->info);

   sscreen->debug_flags = debug_get_flags_option("R600_DEBUG", radeonsi_debug_options, 0);
   sscreen->debug_flags |= debug_get_flags_option("AMD_DEBUG", radeonsi_debug_options, 0);

   if ((sscreen->debug_flags & DBG(TMZ)) &&
       !sscreen->info.has_tmz_support) {
      fprintf(stderr, "radeonsi: requesting TMZ features but TMZ is not supported\n");
      FREE(sscreen);
      return NULL;
   }

   util_idalloc_mt_init_tc(&sscreen->buffer_ids);

   /* Set functions first. */
   sscreen->b.context_create = si_pipe_create_context;
   sscreen->b.destroy = si_destroy_screen;

   si_init_screen_buffer_functions(sscreen);
   si_init_screen_fence_functions(sscreen);
   si_init_screen_texture_functions(sscreen);

   si_init_screen_get_functions(sscreen);
   si_init_screen_caps(sscreen);

   if (sscreen->debug_flags & DBG(INFO))
      ac_print_gpu_info(stdout, &sscreen->info, ws->get_fd(ws));

   slab_create_parent(&sscreen->pool_transfers, sizeof(struct si_transfer), 64);

   (void)simple_mtx_init(&sscreen->print_ib_mutex, mtx_plain);

   if (!si_init_gfx_screen(sscreen)) {
      FREE(sscreen);
      return NULL;
   }
   /* Don't fail if the multimedia support is missing. */
   si_init_mm_screen(sscreen);

   si_init_renderer_string(sscreen);

   for (unsigned i = 0; i < ARRAY_SIZE(sscreen->aux_contexts); i++)
      (void)mtx_init(&sscreen->aux_contexts[i].lock, mtx_plain | mtx_recursive);

   si_run_tests(sscreen);

   return &sscreen->b;
}

struct pipe_screen *radeonsi_screen_create(int fd, const struct pipe_screen_config *config)
{
   struct radeon_winsys *rw = NULL;
   drmVersionPtr version;

   version = drmGetVersion(fd);
   if (!version)
     return NULL;

#if AMD_LLVM_AVAILABLE
   /* LLVM must be initialized before util_queue because both u_queue and LLVM call atexit,
    * and LLVM must call it first because its atexit handler executes C++ destructors,
    * which must be done after our compiler threads using LLVM in u_queue are finished
    * by their atexit handler. Since atexit handlers are called in the reverse order,
    * LLVM must be initialized first, followed by u_queue.
    */
   ac_init_llvm_once();
#endif

   driParseConfigFiles(config->options, config->options_info,
                       &(driConfigFileParseParams) { .driverName = "radeonsi" });

#ifdef HAVE_AMDGPU_VIRTIO
   if (strcmp(version->name, "virtio_gpu") == 0) {
      rw = amdgpu_winsys_create(fd, config, radeonsi_screen_create_impl, true);
   } else if (debug_get_bool_option("AMD_FORCE_VPIPE", false)) {
      rw = amdgpu_winsys_create(-1, config, radeonsi_screen_create_impl, true);
   } else
#endif
   {
      switch (version->version_major) {
      case 2:
         rw = radeon_drm_winsys_create(fd, config, radeonsi_screen_create_impl);
         break;
      case 3:
         rw = amdgpu_winsys_create(fd, config, radeonsi_screen_create_impl, false);
         break;
      }
   }

   drmFreeVersion(version);
   return rw ? rw->screen : NULL;
}
