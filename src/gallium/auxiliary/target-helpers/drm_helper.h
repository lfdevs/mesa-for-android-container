#ifndef DRM_HELPER_H
#define DRM_HELPER_H

#include <stdio.h>
#include "target-helpers/inline_debug_helper.h"
#include "target-helpers/drm_helper_public.h"

#ifdef GALLIUM_I915
#include "i915/drm/i915_drm_public.h"
#include "i915/i915_public.h"

struct pipe_screen *
pipe_i915_create_screen(int fd, unsigned flags)
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
pipe_i915_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "i915g: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_NOUVEAU
#include "nouveau/drm/nouveau_drm_public.h"

struct pipe_screen *
pipe_nouveau_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = nouveau_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_nouveau_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "nouveau: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_PL111
#include "pl111/drm/pl111_drm_public.h"

struct pipe_screen *
pipe_pl111_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = pl111_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_pl111_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "pl111: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_R300
#include "radeon/radeon_winsys.h"
#include "radeon/drm/radeon_drm_public.h"
#include "r300/r300_public.h"

struct pipe_screen *
pipe_r300_create_screen(int fd, unsigned flags)
{
   struct radeon_winsys *rw;

   rw = radeon_drm_winsys_create(fd, flags, r300_screen_create);
   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_r300_create_screen(int fd, unsigned flags)
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
pipe_r600_create_screen(int fd, unsigned flags)
{
   struct radeon_winsys *rw;

   rw = radeon_drm_winsys_create(fd, flags, r600_screen_create);
   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_r600_create_screen(int fd, unsigned flags)
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
pipe_radeonsi_create_screen(int fd, unsigned flags)
{
   struct radeon_winsys *rw;

   /* First, try amdgpu. */
   rw = amdgpu_winsys_create(fd, flags, radeonsi_screen_create);

   if (!rw)
      rw = radeon_drm_winsys_create(fd, flags, radeonsi_screen_create);

   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_radeonsi_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "radeonsi: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VMWGFX
#include "svga/drm/svga_drm_public.h"
#include "svga/svga_public.h"

struct pipe_screen *
pipe_vmwgfx_create_screen(int fd, unsigned flags)
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
pipe_vmwgfx_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "svga: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_FREEDRENO
#include "freedreno/drm/freedreno_drm_public.h"

struct pipe_screen *
pipe_freedreno_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = fd_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_freedreno_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "freedreno: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VIRGL
#include "virgl/drm/virgl_drm_public.h"
#include "virgl/virgl_public.h"

struct pipe_screen *
pipe_virgl_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = virgl_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_virgl_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "virgl: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VC4
#include "vc4/drm/vc4_drm_public.h"

struct pipe_screen *
pipe_vc4_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = vc4_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_vc4_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "vc4: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_ETNAVIV
#include "etnaviv/drm/etnaviv_drm_public.h"

struct pipe_screen *
pipe_etna_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = etna_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_etna_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "etnaviv: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_IMX
#include "imx/drm/imx_drm_public.h"

struct pipe_screen *
pipe_imx_drm_create_screen(int fd, unsigned flags)
{
   struct pipe_screen *screen;

   screen = imx_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_imx_drm_create_screen(int fd, unsigned flags)
{
   fprintf(stderr, "imx-drm: driver missing\n");
   return NULL;
}

#endif


#endif /* DRM_HELPER_H */
