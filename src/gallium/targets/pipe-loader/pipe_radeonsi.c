#include "frontend/drm_driver.h"
#include "target-helpers/inline_debug_helper.h"
#include "radeonsi/si_public.h"
#include "util/driconf.h"

static struct pipe_screen *
create_screen(int fd, const struct pipe_screen_config *config)
{
   struct pipe_screen *screen = radeonsi_screen_create(fd, config);

   return screen ? debug_screen_wrap(screen) : NULL;
}

static const char *driconf_xml =
   #include "radeonsi/si_driinfo.h"
   ;

PUBLIC
DRM_DRIVER_DESCRIPTOR("radeonsi", &driconf_xml, create_screen)
