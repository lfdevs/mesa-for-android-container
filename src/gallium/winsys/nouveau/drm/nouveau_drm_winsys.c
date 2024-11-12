#include <unistd.h>

#include "util/format/u_format.h"
#include "util/os_file.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_screen.h"

#include "renderonly/renderonly.h"
#include "nouveau_drm_public.h"

#include "nouveau/nouveau_screen.h"
#include "nouveau/nouveau_winsys.h"

static struct pipe_screen *
nouveau_screen_create(int fd, const struct pipe_screen_config *config,
                      struct renderonly *ro)
{
   struct nouveau_drm *drm = NULL;
   struct nouveau_device *dev = NULL;
   struct nouveau_screen *(*init)(struct nouveau_device *);
   struct nouveau_screen *screen = NULL;
   int ret;

   ret = nouveau_drm_new(fd, &drm);
   if (ret)
      return NULL;

   ret = nouveau_device_new(&drm->client, &dev);
   if (ret)
      goto err_dev_new;

   switch (dev->chipset & ~0xf) {
   case 0x30:
   case 0x40:
   case 0x60:
      init = nv30_screen_create;
      break;
   case 0x50:
   case 0x80:
   case 0x90:
   case 0xa0:
      init = nv50_screen_create;
      break;
   case 0xc0:
   case 0xd0:
   case 0xe0:
   case 0xf0:
   case 0x100:
   case 0x110:
   case 0x120:
   case 0x130:
   case 0x140:
   case 0x160:
   case 0x170:
   case 0x190:
      init = nvc0_screen_create;
      break;
   default:
      debug_printf("%s: unknown chipset nv%02x\n", __func__, dev->chipset);
      goto err_screen_create;
   }

   screen = init(dev);
   if (!screen)
      goto err_screen_create;

   if (!screen->base.context_create)
      goto err_screen_init;

   screen->initialized = true;
   return &screen->base;

err_screen_init:
   screen->base.destroy(&screen->base);
   return NULL;

err_screen_create:
   nouveau_device_del(&dev);
err_dev_new:
   nouveau_drm_del(&drm);
   return NULL;
}

PUBLIC struct pipe_screen *
nouveau_drm_screen_create(int fd)
{
   return u_pipe_screen_lookup_or_create(os_dupfd_cloexec(fd), NULL, NULL,
                                         nouveau_screen_create);
}
