#ifndef DRM_HELPER_H
#define DRM_HELPER_H

#include <stdio.h>
#include "target-helpers/inline_debug_helper.h"
#include "target-helpers/drm_helper_public.h"

#ifdef GALLIUM_I915
#include "i915/drm/i915_drm_public.h"
#include "i915/i915_public.h"

struct pipe_screen *
pipe_i915_create_screen(int fd)
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
pipe_i915_create_screen(int fd)
{
   fprintf(stderr, "i915g: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_ILO
#include "intel/drm/intel_drm_public.h"
#include "ilo/ilo_public.h"

struct pipe_screen *
pipe_ilo_create_screen(int fd)
{
   struct intel_winsys *iws;
   struct pipe_screen *screen;

   iws = intel_winsys_create_for_fd(fd);
   if (!iws)
      return NULL;

   screen = ilo_screen_create(iws);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_ilo_create_screen(int fd)
{
   fprintf(stderr, "ilo: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_NOUVEAU
#include "nouveau/drm/nouveau_drm_public.h"

struct pipe_screen *
pipe_nouveau_create_screen(int fd)
{
   struct pipe_screen *screen;

   screen = nouveau_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_nouveau_create_screen(int fd)
{
   fprintf(stderr, "nouveau: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_R300
#include "radeon/radeon_winsys.h"
#include "radeon/drm/radeon_drm_public.h"
#include "r300/r300_public.h"

struct pipe_screen *
pipe_r300_create_screen(int fd)
{
   struct radeon_winsys *rw;

   rw = radeon_drm_winsys_create(fd, r300_screen_create);
   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_r300_create_screen(int fd)
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
pipe_r600_create_screen(int fd)
{
   struct radeon_winsys *rw;

   rw = radeon_drm_winsys_create(fd, r600_screen_create);
   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_r600_create_screen(int fd)
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
pipe_radeonsi_create_screen(int fd)
{
   struct radeon_winsys *rw;

   /* First, try amdgpu. */
   rw = amdgpu_winsys_create(fd, radeonsi_screen_create);

   if (!rw)
      rw = radeon_drm_winsys_create(fd, radeonsi_screen_create);

   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

#else

struct pipe_screen *
pipe_radeonsi_create_screen(int fd)
{
   fprintf(stderr, "radeonsi: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VMWGFX
#include "svga/drm/svga_drm_public.h"
#include "svga/svga_public.h"

struct pipe_screen *
pipe_vmwgfx_create_screen(int fd)
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
pipe_vmwgfx_create_screen(int fd)
{
   fprintf(stderr, "svga: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_FREEDRENO
#include "freedreno/drm/freedreno_drm_public.h"

struct pipe_screen *
pipe_freedreno_create_screen(int fd)
{
   struct pipe_screen *screen;

   screen = fd_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_freedreno_create_screen(int fd)
{
   fprintf(stderr, "freedreno: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VIRGL
#include "virgl/drm/virgl_drm_public.h"
#include "virgl/virgl_public.h"

struct pipe_screen *
pipe_virgl_create_screen(int fd)
{
   struct virgl_winsys *vws;
   struct pipe_screen *screen;

   vws = virgl_drm_winsys_create(fd);
   if (!vws)
      return NULL;

   screen = virgl_create_screen(vws);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_virgl_create_screen(int fd)
{
   fprintf(stderr, "virgl: driver missing\n");
   return NULL;
}

#endif

#ifdef GALLIUM_VC4
#include "vc4/drm/vc4_drm_public.h"

struct pipe_screen *
pipe_vc4_create_screen(int fd)
{
   struct pipe_screen *screen;

   screen = vc4_drm_screen_create(fd);
   return screen ? debug_screen_wrap(screen) : NULL;
}

#else

struct pipe_screen *
pipe_vc4_create_screen(int fd)
{
   fprintf(stderr, "vc4: driver missing\n");
   return NULL;
}

#endif


#endif /* DRM_HELPER_H */
