/*
 * Copyright (c) 2019 Etnaviv Project
 * Copyright (c) 2019 Zodiac Inflight Innovations
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "drm-uapi/etnaviv_drm.h"
#include "drm-shim/drm_shim.h"
#include "util/u_debug.h"

struct etna_shim_gpu
{
   uint32_t model;
   uint32_t revision;
   uint32_t product_id;
   uint32_t customer_id;
   uint32_t eco_id;
};

/* The GPU a bare LD_PRELOAD emulated before ETNA_SHIM_GPU took an identity. */
#define ETNA_SHIM_GPU_DEFAULT "2000:5108"

/* Where the kernel would let the GPU allocate from. It describes the address
 * space rather than the chip, so the hardware database has no opinion on it,
 * and HALTI5 is refused without one.
 */
#define ETNA_SHIM_SOFTPIN_START_ADDR 0x00400000

static struct etna_shim_gpu shim_gpu;

static int
etnaviv_ioctl_noop(int fd, unsigned long request, void *arg)
{
   return 0;
}

static int
etnaviv_ioctl_pm_query_dom(int fd, unsigned long request, void *arg)
{
   struct drm_etnaviv_pm_domain *args = arg;

   args->iter = 0xff;
   args->nr_signals = 0;

   return 0;
}

static int
etnaviv_ioctl_gem_new(int fd, unsigned long request, void *arg)
{
   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   struct drm_etnaviv_gem_new *create = arg;
   struct shim_bo *bo = calloc(1, sizeof(*bo));

   drm_shim_bo_init(bo, create->size);
   create->handle = drm_shim_bo_get_handle(shim_fd, bo);
   drm_shim_bo_put(bo);

   return 0;
}

static int
etnaviv_ioctl_gem_info(int fd, unsigned long request, void *arg)
{
   struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
   struct drm_etnaviv_gem_info *args = arg;
   struct shim_bo *bo = drm_shim_bo_lookup(shim_fd, args->handle);

   args->offset = drm_shim_bo_get_mmap_offset(shim_fd, bo);
   drm_shim_bo_put(bo);

   return 0;
}

static int
etnaviv_ioctl_get_param(int fd, unsigned long request, void *arg)
{
   struct drm_etnaviv_param *gp = arg;

   /* Only one pipe is modelled. */
   if (gp->pipe != 0) {
      errno = -ENXIO;
      return 0;
   }

   switch (gp->param) {
   case ETNAVIV_PARAM_GPU_MODEL:
      gp->value = shim_gpu.model;
      break;
   case ETNAVIV_PARAM_GPU_REVISION:
      gp->value = shim_gpu.revision;
      break;
   case ETNAVIV_PARAM_GPU_PRODUCT_ID:
      gp->value = shim_gpu.product_id;
      break;
   case ETNAVIV_PARAM_GPU_CUSTOMER_ID:
      gp->value = shim_gpu.customer_id;
      break;
   case ETNAVIV_PARAM_GPU_ECO_ID:
      gp->value = shim_gpu.eco_id;
      break;
   case ETNAVIV_PARAM_SOFTPIN_START_ADDR:
      gp->value = ETNA_SHIM_SOFTPIN_START_ADDR;
      break;
   case ETNAVIV_PARAM_GPU_FEATURES_0 ... ETNAVIV_PARAM_GPU_NUM_VARYINGS:
      /* Only asked for when the database had no entry for the identity. */
      fprintf(stderr, "No hardware database entry for ETNA_SHIM_GPU=%x:%x:%x:%x:%x\n",
              shim_gpu.model, shim_gpu.revision, shim_gpu.product_id,
              shim_gpu.customer_id, shim_gpu.eco_id);
      exit(1);
   default:
      errno = -EINVAL;
      return -1;
   }

   errno = 0;
   return 0;
}

static ioctl_fn_t driver_ioctls[] = {
   [DRM_ETNAVIV_GET_PARAM] = etnaviv_ioctl_get_param,
   [DRM_ETNAVIV_GEM_NEW] = etnaviv_ioctl_gem_new,
   [DRM_ETNAVIV_GEM_INFO] = etnaviv_ioctl_gem_info,
   [DRM_ETNAVIV_GEM_CPU_PREP] = etnaviv_ioctl_noop,
   [DRM_ETNAVIV_GEM_CPU_FINI] = etnaviv_ioctl_noop,
   [DRM_ETNAVIV_GEM_SUBMIT] = etnaviv_ioctl_noop,
   [DRM_ETNAVIV_WAIT_FENCE] = etnaviv_ioctl_noop,
   [DRM_ETNAVIV_GEM_USERPTR] = etnaviv_ioctl_noop,
   [DRM_ETNAVIV_GEM_WAIT] = etnaviv_ioctl_noop,
   [DRM_ETNAVIV_PM_QUERY_DOM] = etnaviv_ioctl_pm_query_dom,
   [DRM_ETNAVIV_PM_QUERY_SIG] = etnaviv_ioctl_noop,
};

static bool
parse_identity(const char *spec, struct etna_shim_gpu *gpu)
{
   uint32_t id[5] = { 0 };
   const char *pos = spec;
   unsigned n = 0;

   while (n < ARRAY_SIZE(id)) {
      char *end;

      id[n++] = strtoul(pos, &end, 16);
      if (end == pos)
         return false;

      pos = end;
      if (*pos != ':')
         break;

      pos++;
   }

   if (*pos != '\0' || n < 2)
      return false;

   gpu->model = id[0];
   gpu->revision = id[1];
   gpu->product_id = id[2];
   gpu->customer_id = id[3];
   gpu->eco_id = id[4];

   return true;
}

void
drm_shim_driver_init(void)
{
   shim_device.driver_ioctls = driver_ioctls;
   shim_device.driver_ioctl_count = ARRAY_SIZE(driver_ioctls);

   /* Report a version that answers the GPU identity, so that the driver takes
    * features and limits from the hardware database.
    */
   shim_device.version_major = 1;
   shim_device.version_minor = 4;
   shim_device.version_patchlevel = 0;

   drm_shim_platform_device_setup("etnaviv", "/soc/gpu3d", "vivante,gc");

   const char *spec = debug_get_option("ETNA_SHIM_GPU", ETNA_SHIM_GPU_DEFAULT);

   if (!parse_identity(spec, &shim_gpu)) {
      fprintf(stderr, "ETNA_SHIM_GPU=%s is not an identity of the form "
              "model:revision[:product:customer:eco]\n", spec);
      exit(1);
   }

   fprintf(stderr, "Using %s as shim gpu\n", spec);
}
