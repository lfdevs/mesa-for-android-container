/*
 * Copyright (C) 2016 Christian Gmeiner <christian.gmeiner@gmail.com>
 * Copyright (C) 2017 Broadcom
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

#include <fcntl.h>
#include <unistd.h>

#include "kmsro_drm_public.h"
#include "v3d/drm/v3d_drm_public.h"
#include "vc4/drm/vc4_drm_public.h"
#include "etnaviv/drm/etnaviv_drm_public.h"
#include "freedreno/drm/freedreno_drm_public.h"
#include "panfrost/drm/panfrost_drm_public.h"
#include "lima/drm/lima_drm_public.h"
#include "asahi/drm/asahi_drm_public.h"
#include "xf86drm.h"

#include "pipe/p_screen.h"
#include "renderonly/renderonly.h"
#include "util/u_memory.h"

static void kmsro_ro_destroy(struct renderonly *ro)
{
   if (ro->gpu_fd >= 0)
      close(ro->gpu_fd);

   util_sparse_array_finish(&ro->bo_map);

   FREE(ro);
}

struct pipe_screen *kmsro_drm_screen_create(int fd,
                                            const struct pipe_screen_config *config)
{
   struct pipe_screen *screen = NULL;
   struct renderonly *ro = CALLOC_STRUCT(renderonly);

   if (!ro)
      return NULL;

   ro->kms_fd = fd;
   ro->gpu_fd = -1;
   ro->destroy = kmsro_ro_destroy;
   util_sparse_array_init(&ro->bo_map, sizeof(struct renderonly_scanout), 64);
   simple_mtx_init(&ro->bo_map_lock, mtx_plain);

   const struct {
      const char *name;
      struct pipe_screen *(*create_screen)(int, struct renderonly *,
                                           const struct pipe_screen_config *);
      struct renderonly_scanout *(*create_for_resource)(struct pipe_resource *,
                                                        struct renderonly *,
                                                        struct winsys_handle *);
   } renderonly_drivers[] = {
#if defined(GALLIUM_ASAHI)
      {
         .name = "asahi",
         .create_screen = asahi_drm_screen_create_renderonly,
         .create_for_resource = renderonly_create_gpu_import_for_resource,
      },
#endif

#if defined(GALLIUM_ETNAVIV)
      {
         .name = "etnaviv",
         .create_screen = etna_drm_screen_create_renderonly,
         .create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      },
#endif

#if defined(GALLIUM_FREEDRENO)
      {
         .name = "msm",
         .create_screen = fd_drm_screen_create_renderonly,
         .create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      },
#endif

#if defined(GALLIUM_LIMA)
      {
         .name = "lima",
         .create_screen = lima_drm_screen_create_renderonly,
         .create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      },
#endif

#if defined(GALLIUM_PANFROST)
      {
         .name = "panfrost",
         .create_screen = panfrost_drm_screen_create_renderonly,
         .create_for_resource = panfrost_create_kms_dumb_buffer_for_resource,
      },
#endif

#if defined(GALLIUM_V3D)
      {
         .name = "v3d",
         .create_screen = v3d_drm_screen_create_renderonly,
         .create_for_resource = renderonly_create_kms_dumb_buffer_for_resource,
      },
#endif

#if defined(GALLIUM_VC4)
      /* Passes the vc4-allocated BO through to the KMS-only DRM device using
       * PRIME buffer sharing.  The VC4 BO must be linear, which the SCANOUT
       * flag on allocation will have ensured.
       */
      {
         .name = "vc4",
         .create_screen = vc4_drm_screen_create_renderonly,
         .create_for_resource = renderonly_create_gpu_import_for_resource,
      },
#endif
   };

   for (int i = 0; i < ARRAY_SIZE(renderonly_drivers); i++) {
      ro->gpu_fd = drmOpenWithType(renderonly_drivers[i].name, NULL, DRM_NODE_RENDER);
      if (ro->gpu_fd >= 0) {
         ro->create_for_resource = renderonly_drivers[i].create_for_resource;
         screen = renderonly_drivers[i].create_screen(ro->gpu_fd, ro, config);
         if (!screen)
            goto out_free;
         return screen;
      }
   }

   return screen;

out_free:
   if (ro->gpu_fd >= 0)
      close(ro->gpu_fd);
   FREE(ro);

   return NULL;
}
