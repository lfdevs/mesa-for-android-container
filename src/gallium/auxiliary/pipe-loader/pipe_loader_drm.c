/**************************************************************************
 *
 * Copyright 2011 Intel Corporation
 * Copyright 2012 Francisco Jerez
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kristian Høgsberg <krh@bitplanet.net>
 *    Benjamin Franzke <benjaminfranzke@googlemail.com>
 *
 **************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <xf86drm.h>
#include <unistd.h>

#include "loader.h"
#include "target-helpers/drm_helper_public.h"
#include "state_tracker/drm_driver.h"
#include "pipe_loader_priv.h"

#include "util/u_memory.h"
#include "util/u_dl.h"
#include "util/u_debug.h"

#define DRM_RENDER_NODE_DEV_NAME_FORMAT "%s/renderD%d"
#define DRM_RENDER_NODE_MAX_NODES 63
#define DRM_RENDER_NODE_MIN_MINOR 128
#define DRM_RENDER_NODE_MAX_MINOR (DRM_RENDER_NODE_MIN_MINOR + DRM_RENDER_NODE_MAX_NODES)

struct pipe_loader_drm_device {
   struct pipe_loader_device base;
   const struct drm_driver_descriptor *dd;
#ifndef GALLIUM_STATIC_TARGETS
   struct util_dl_library *lib;
#endif
   int fd;
};

#define pipe_loader_drm_device(dev) ((struct pipe_loader_drm_device *)dev)

static const struct pipe_loader_ops pipe_loader_drm_ops;

#ifdef GALLIUM_STATIC_TARGETS
static const struct drm_conf_ret throttle_ret = {
   DRM_CONF_INT,
   {2},
};

static const struct drm_conf_ret share_fd_ret = {
   DRM_CONF_BOOL,
   {true},
};

static inline const struct drm_conf_ret *
configuration_query(enum drm_conf conf)
{
   switch (conf) {
   case DRM_CONF_THROTTLE:
      return &throttle_ret;
   case DRM_CONF_SHARE_FD:
      return &share_fd_ret;
   default:
      break;
   }
   return NULL;
}

static const struct drm_driver_descriptor driver_descriptors[] = {
    {
        .driver_name = "i915",
        .create_screen = pipe_i915_create_screen,
        .configuration = configuration_query,
    },
#ifdef USE_VC4_SIMULATOR
    /* VC4 simulator and ILO (i965) are mutually exclusive (error at
     * configure). As the latter is unconditionally added, keep this one above
     * it.
     */
    {
        .driver_name = "i965",
        .create_screen = pipe_vc4_create_screen,
        .configuration = configuration_query,
    },
#endif
    {
        .driver_name = "i965",
        .create_screen = pipe_ilo_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "nouveau",
        .create_screen = pipe_nouveau_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "r300",
        .create_screen = pipe_r300_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "r600",
        .create_screen = pipe_r600_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "radeonsi",
        .create_screen = pipe_radeonsi_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "vmwgfx",
        .create_screen = pipe_vmwgfx_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "kgsl",
        .create_screen = pipe_freedreno_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "msm",
        .create_screen = pipe_freedreno_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "virtio_gpu",
        .create_screen = pipe_virgl_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "vc4",
        .create_screen = pipe_vc4_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "etnaviv",
        .create_screen = pipe_etna_create_screen,
        .configuration = configuration_query,
    },
    {
        .driver_name = "imx-drm",
        .create_screen = pipe_imx_drm_create_screen,
        .configuration = configuration_query,
    }
};
#endif

bool
pipe_loader_drm_probe_fd(struct pipe_loader_device **dev, int fd)
{
   struct pipe_loader_drm_device *ddev = CALLOC_STRUCT(pipe_loader_drm_device);
   int vendor_id, chip_id;

   if (!ddev)
      return false;

   if (loader_get_pci_id_for_fd(fd, &vendor_id, &chip_id)) {
      ddev->base.type = PIPE_LOADER_DEVICE_PCI;
      ddev->base.u.pci.vendor_id = vendor_id;
      ddev->base.u.pci.chip_id = chip_id;
   } else {
      ddev->base.type = PIPE_LOADER_DEVICE_PLATFORM;
   }
   ddev->base.ops = &pipe_loader_drm_ops;
   ddev->fd = fd;

   ddev->base.driver_name = loader_get_driver_for_fd(fd);
   if (!ddev->base.driver_name)
      goto fail;

#ifdef GALLIUM_STATIC_TARGETS
   for (int i = 0; i < ARRAY_SIZE(driver_descriptors); i++) {
      if (strcmp(driver_descriptors[i].driver_name, ddev->base.driver_name) == 0) {
         ddev->dd = &driver_descriptors[i];
         break;
      }
   }
   if (!ddev->dd)
      goto fail;
#else
   ddev->lib = pipe_loader_find_module(&ddev->base, PIPE_SEARCH_DIR);
   if (!ddev->lib)
      goto fail;

   ddev->dd = (const struct drm_driver_descriptor *)
      util_dl_get_proc_address(ddev->lib, "driver_descriptor");

   /* sanity check on the driver name */
   if (!ddev->dd || strcmp(ddev->dd->driver_name, ddev->base.driver_name) != 0)
      goto fail;
#endif

   *dev = &ddev->base;
   return true;

  fail:
#ifndef GALLIUM_STATIC_TARGETS
   if (ddev->lib)
      util_dl_close(ddev->lib);
#endif
   FREE(ddev);
   return false;
}

static int
open_drm_render_node_minor(int minor)
{
   char path[PATH_MAX];
   snprintf(path, sizeof(path), DRM_RENDER_NODE_DEV_NAME_FORMAT, DRM_DIR_NAME,
            minor);
   return loader_open_device(path);
}

int
pipe_loader_drm_probe(struct pipe_loader_device **devs, int ndev)
{
   int i, j, fd;

   for (i = DRM_RENDER_NODE_MIN_MINOR, j = 0;
        i <= DRM_RENDER_NODE_MAX_MINOR; i++) {
      struct pipe_loader_device *dev;

      fd = open_drm_render_node_minor(i);
      if (fd < 0)
         continue;

      if (!pipe_loader_drm_probe_fd(&dev, fd)) {
         close(fd);
         continue;
      }

      if (j < ndev) {
         devs[j] = dev;
      } else {
         close(fd);
         dev->ops->release(&dev);
      }
      j++;
   }

   return j;
}

static void
pipe_loader_drm_release(struct pipe_loader_device **dev)
{
   struct pipe_loader_drm_device *ddev = pipe_loader_drm_device(*dev);

#ifndef GALLIUM_STATIC_TARGETS
   if (ddev->lib)
      util_dl_close(ddev->lib);
#endif

   close(ddev->fd);
   FREE(ddev->base.driver_name);
   FREE(ddev);
   *dev = NULL;
}

static const struct drm_conf_ret *
pipe_loader_drm_configuration(struct pipe_loader_device *dev,
                              enum drm_conf conf)
{
   struct pipe_loader_drm_device *ddev = pipe_loader_drm_device(dev);

   if (!ddev->dd->configuration)
      return NULL;

   return ddev->dd->configuration(conf);
}

static struct pipe_screen *
pipe_loader_drm_create_screen(struct pipe_loader_device *dev)
{
   struct pipe_loader_drm_device *ddev = pipe_loader_drm_device(dev);

   return ddev->dd->create_screen(ddev->fd);
}

static const struct pipe_loader_ops pipe_loader_drm_ops = {
   .create_screen = pipe_loader_drm_create_screen,
   .configuration = pipe_loader_drm_configuration,
   .release = pipe_loader_drm_release
};
