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
   struct util_dl_library *lib;
   int fd;
};

#define pipe_loader_drm_device(dev) ((struct pipe_loader_drm_device *)dev)

static struct pipe_loader_ops pipe_loader_drm_ops;

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

   ddev->base.driver_name = loader_get_driver_for_fd(fd, _LOADER_GALLIUM);
   if (!ddev->base.driver_name)
      goto fail;

   *dev = &ddev->base;
   return true;

  fail:
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
      fd = open_drm_render_node_minor(i);
      struct pipe_loader_device *dev;
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

   if (ddev->lib)
      util_dl_close(ddev->lib);

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
   const struct drm_driver_descriptor *dd;

   if (!ddev->lib)
      return NULL;

   dd = (const struct drm_driver_descriptor *)
      util_dl_get_proc_address(ddev->lib, "driver_descriptor");

   /* sanity check on the name */
   if (!dd || strcmp(dd->name, ddev->base.driver_name) != 0)
      return NULL;

   if (!dd->configuration)
      return NULL;

   return dd->configuration(conf);
}

static struct pipe_screen *
pipe_loader_drm_create_screen(struct pipe_loader_device *dev,
                              const char *library_paths)
{
   struct pipe_loader_drm_device *ddev = pipe_loader_drm_device(dev);
   const struct drm_driver_descriptor *dd;

   if (!ddev->lib)
      ddev->lib = pipe_loader_find_module(dev, library_paths);
   if (!ddev->lib)
      return NULL;

   dd = (const struct drm_driver_descriptor *)
      util_dl_get_proc_address(ddev->lib, "driver_descriptor");

   /* sanity check on the name */
   if (!dd || strcmp(dd->name, ddev->base.driver_name) != 0)
      return NULL;

   return dd->create_screen(ddev->fd);
}

static struct pipe_loader_ops pipe_loader_drm_ops = {
   .create_screen = pipe_loader_drm_create_screen,
   .configuration = pipe_loader_drm_configuration,
   .release = pipe_loader_drm_release
};
