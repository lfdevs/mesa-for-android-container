#ifndef DRM_HELPER_H
#define DRM_HELPER_H

#include <stdio.h>
#include "target-helpers/inline_debug_helper.h"
#include "target-helpers/drm_helper_public.h"
#include "state_tracker/drm_driver.h"
#include "util/xmlpool.h"

#ifdef GALLIUM_I915
#include "i915/drm/i915_drm_public.h"
#include "i915/i915_public.h"

struct pipe_screen *
pipe_i915_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct i915_winsys *iws;
   struct pipe_screen *screen;

   iws = i915_drm_winsys_create(fd);
   if (!iws)
      return NULL;

   screen = i915_screen_create(iws);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_i915_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "i915g: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_IRIS
#include "iris/drm/iris_drm_public.h"

struct pipe_screen *
pipe_iris_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = iris_drm_screen_create(fd, config);
   return screen ? debug_screen_wrap(screen) : NULL;
}

const char *iris_driconf_xml =
      #include "iris/iris_driinfo.h"
      ;

#else

struct pipe_screen *
pipe_iris_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "iris: driver missing\n");
   return NULL;
}

const char *iris_driconf_xml = NULL;

#endif

#ifdef GALLIUM_NOUVEAU
#include "nouveau/drm/nouveau_drm_public.h"

struct pipe_screen *
pipe_nouveau_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = nouveau_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_nouveau_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "nouveau: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_KMSRO
#include "kmsro/drm/kmsro_drm_public.h"

struct pipe_screen *
pipe_kmsro_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = kmsro_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_kmsro_create_screen(int fd, const struct pipe_screen_config *config)
{
   return NULL;
}

#endif

#ifdef GALLIUM_R300
#include "radeon/radeon_winsys.h"
#include "radeon/drm/radeon_drm_public.h"
#include "r300/r300_public.h"

struct pipe_screen *
pipe_r300_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct radeon_winsys *rw;

   rw = radeon_drm_winsys_create(fd, config, r300_screen_create);
   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_r300_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "r300: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_R600
#include "radeon/radeon_winsys.h"
#include "radeon/drm/radeon_drm_public.h"
#include "r600/r600_public.h"

struct pipe_screen *
pipe_r600_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct radeon_winsys *rw;

   rw = radeon_drm_winsys_create(fd, config, r600_screen_create);
   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_r600_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "r600: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_RADEONSI
#include "radeon/radeon_winsys.h"
#include "radeon/drm/radeon_drm_public.h"
#include "amdgpu/drm/amdgpu_public.h"
#include "radeonsi/si_public.h"

struct pipe_screen *
pipe_radeonsi_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct radeon_winsys *rw;

   /* First, try amdgpu. */
   rw = amdgpu_winsys_create(fd, config, radeonsi_screen_create);

   if (!rw)
      rw = radeon_drm_winsys_create(fd, config, radeonsi_screen_create);

   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

const char *radeonsi_driconf_xml =
      #include "radeonsi/si_driinfo.h"
      ;

#else

struct pipe_screen *
pipe_radeonsi_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "radeonsi: driver missing\n");
   return NULL;
}

const char *radeonsi_driconf_xml = NULL;

#endif

#ifdef GALLIUM_VMWGFX
#include "svga/drm/svga_drm_public.h"
#include "svga/svga_public.h"

struct pipe_screen *
pipe_vmwgfx_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct svga_winsys_screen *sws;
   struct pipe_screen *screen;

   sws = svga_drm_winsys_screen_create(fd);
   if (!sws)
      return NULL;

   screen = svga_screen_create(sws);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_vmwgfx_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "svga: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_FREEDRENO
#include "freedreno/drm/freedreno_drm_public.h"

struct pipe_screen *
pipe_freedreno_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = fd_drm_screen_create(fd, NULL);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_freedreno_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "freedreno: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VIRGL
#include "virgl/drm/virgl_drm_public.h"
#include "virgl/virgl_public.h"

struct pipe_screen *
pipe_virgl_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = virgl_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_virgl_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "virgl: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VC4
#include "vc4/drm/vc4_drm_public.h"

struct pipe_screen *
pipe_vc4_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = vc4_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_vc4_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "vc4: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_V3D
#include "v3d/drm/v3d_drm_public.h"

struct pipe_screen *
pipe_v3d_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = v3d_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_v3d_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "v3d: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_PANFROST
#include "panfrost/drm/panfrost_drm_public.h"

struct pipe_screen *
pipe_panfrost_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = panfrost_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_panfrost_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "panfrost: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_ETNAVIV
#include "etnaviv/drm/etnaviv_drm_public.h"

struct pipe_screen *
pipe_etna_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = etna_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_etna_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "etnaviv: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_TEGRA
#include "tegra/drm/tegra_drm_public.h"

struct pipe_screen *
pipe_tegra_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = tegra_drm_screen_create(fd);

   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_tegra_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "tegra: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_LIMA
#include "lima/drm/lima_drm_public.h"

struct pipe_screen *
pipe_lima_create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen;

   screen = lima_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_lima_create_screen(int fd, const struct pipe_screen_config *config)
{
   fprintf(stderr, "lima: driver missing\n");
   return NULL;
}

#endif

#endif /* DRM_HELPER_H */
