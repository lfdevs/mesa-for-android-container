#include "state_tracker/drm_driver.h"
#include "target-helpers/inline_debug_helper.h"
#include "radeon/drm/radeon_drm_public.h"
#include "radeon/radeon_winsys.h"
#include "amdgpu/drm/amdgpu_public.h"
#include "radeonsi/si_public.h"
#include "util/xmlpool.h"

static struct pipe_screen *
create_screen(int fd, const struct pipe_screen_config *config)
{
   struct radeon_winsys *rw;

   /* First, try amdgpu. */
   rw = amdgpu_winsys_create(fd, config, radeonsi_screen_create);

   if (!rw)
      rw = radeon_drm_winsys_create(fd, config, radeonsi_screen_create);

   return rw ? debug_screen_wrap(rw->screen) : NULL;
}

static const char *driconf_xml =
   #include "radeonsi/si_driinfo.h"
   ;

PUBLIC
DRM_DRIVER_DESCRIPTOR("radeonsi", &driconf_xml, create_screen)
