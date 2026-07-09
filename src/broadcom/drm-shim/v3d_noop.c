/*
 * Copyright © 2018 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include "drm-uapi/v3d_drm.h"
#include "drm-shim/drm_shim.h"
#include "util/log.h"
#include "util/u_debug.h"

struct v3d_bo {
        struct shim_bo base;
        uint32_t offset;
};

struct v3d_shim_gpu {
        const char *name;
        const uint32_t *reg_map;
};

static const struct v3d_shim_gpu gpus[] = {
        {
                .name = "71",
                .reg_map = (const uint32_t[]) {
                        [DRM_V3D_PARAM_V3D_UIFCFG]       = 0x00000045,
                        [DRM_V3D_PARAM_V3D_HUB_IDENT1]   = 0x00081117,
                        [DRM_V3D_PARAM_V3D_HUB_IDENT2]   = 0x00001900,
                        [DRM_V3D_PARAM_V3D_HUB_IDENT3]   = 0x00000700,
                        [DRM_V3D_PARAM_V3D_CORE0_IDENT0] = 0x07443356,
                        [DRM_V3D_PARAM_V3D_CORE0_IDENT1] = 0x81001441,
                        [DRM_V3D_PARAM_V3D_CORE0_IDENT2] = 0xc0078101,
                }
        },
        {
                .name = "42",
                .reg_map = (const uint32_t[]) {
                        [DRM_V3D_PARAM_V3D_UIFCFG]       = 0x00000045,
                        [DRM_V3D_PARAM_V3D_HUB_IDENT1]   = 0x000e1124,
                        [DRM_V3D_PARAM_V3D_HUB_IDENT2]   = 0x00000100,
                        [DRM_V3D_PARAM_V3D_HUB_IDENT3]   = 0x00000e00,
                        [DRM_V3D_PARAM_V3D_CORE0_IDENT0] = 0x04443356,
                        [DRM_V3D_PARAM_V3D_CORE0_IDENT1] = 0x81001422,
                        [DRM_V3D_PARAM_V3D_CORE0_IDENT2] = 0x40078121,
                }
        },
};

static const struct v3d_shim_gpu *shim_gpu = &gpus[0];

static struct v3d_bo *
v3d_bo(struct shim_bo *bo)
{
        return (struct v3d_bo *)bo;
}

struct v3d_device {
        uint32_t next_offset;
};

static struct v3d_device v3d = {
        .next_offset = 0x1000,
};

static int
v3d_ioctl_noop(int fd, unsigned long request, void *arg)
{
        return 0;
}

static int
v3d_ioctl_create_bo(int fd, unsigned long request, void *arg)
{
        struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
        struct drm_v3d_create_bo *create = arg;
        struct v3d_bo *bo = calloc(1, sizeof(*bo));

        drm_shim_bo_init(&bo->base, create->size);

        assert(UINT_MAX - v3d.next_offset > create->size);
        bo->offset = v3d.next_offset;
        v3d.next_offset += create->size;

        create->offset = bo->offset;
        create->handle = drm_shim_bo_get_handle(shim_fd, &bo->base);

        drm_shim_bo_put(&bo->base);

        return 0;
}

static int
v3d_ioctl_get_bo_offset(int fd, unsigned long request, void *arg)
{
        struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
        struct drm_v3d_get_bo_offset *args = arg;
        struct shim_bo *bo = drm_shim_bo_lookup(shim_fd, args->handle);

        args->offset = v3d_bo(bo)->offset;

        drm_shim_bo_put(bo);

        return 0;
}

static int
v3d_ioctl_mmap_bo(int fd, unsigned long request, void *arg)
{
        struct shim_fd *shim_fd = drm_shim_fd_lookup(fd);
        struct drm_v3d_mmap_bo *map = arg;
        struct shim_bo *bo = drm_shim_bo_lookup(shim_fd, map->handle);

        map->offset = drm_shim_bo_get_mmap_offset(shim_fd, bo);

        drm_shim_bo_put(bo);

        return 0;
}

static int
v3d_ioctl_get_param(int fd, unsigned long request, void *arg)
{
        struct drm_v3d_get_param *gp = arg;

        switch (gp->param) {
        case DRM_V3D_PARAM_SUPPORTS_TFU:
                gp->value = 1;
                return 0;
        case DRM_V3D_PARAM_SUPPORTS_CSD:
                gp->value = 1;
                return 0;
        case DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH:
                gp->value = 1;
                return 0;
        case DRM_V3D_PARAM_SUPPORTS_PERFMON:
                gp->value = 1;
                return 0;
        case DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT:
                gp->value = 1;
                return 0;
	case DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE:
		gp->value = 1;
                return 0;
	case DRM_V3D_PARAM_MAX_PERF_COUNTERS:
		gp->value = 0;
                return 0;
        case DRM_V3D_PARAM_GLOBAL_RESET_COUNTER:
                gp->value = 0;
                return 0;
        case DRM_V3D_PARAM_CONTEXT_RESET_COUNTER:
                gp->value = 0;
                return 0;
        default:
                break;
        }

        if (gp->param <= DRM_V3D_PARAM_V3D_CORE0_IDENT2) {
                gp->value = shim_gpu->reg_map[gp->param];
                return 0;
        }

        mesa_loge("Unknown DRM_IOCTL_V3D_GET_PARAM %d", gp->param);
        return -1;
}

static ioctl_fn_t driver_ioctls[] = {
        [DRM_V3D_SUBMIT_CL] = v3d_ioctl_noop,
        [DRM_V3D_SUBMIT_TFU] = v3d_ioctl_noop,
        [DRM_V3D_SUBMIT_CSD] = v3d_ioctl_noop,
        [DRM_V3D_SUBMIT_CPU] = v3d_ioctl_noop,
        [DRM_V3D_PERFMON_CREATE] = v3d_ioctl_noop,
        [DRM_V3D_PERFMON_DESTROY] = v3d_ioctl_noop,
        [DRM_V3D_PERFMON_GET_VALUES] = v3d_ioctl_noop,
        [DRM_V3D_PERFMON_GET_COUNTER] = v3d_ioctl_noop,
        [DRM_V3D_PERFMON_SET_GLOBAL] = v3d_ioctl_noop,
        [DRM_V3D_WAIT_BO] = v3d_ioctl_noop,
        [DRM_V3D_CREATE_BO] = v3d_ioctl_create_bo,
        [DRM_V3D_GET_PARAM] = v3d_ioctl_get_param,
        [DRM_V3D_GET_BO_OFFSET] = v3d_ioctl_get_bo_offset,
        [DRM_V3D_MMAP_BO] = v3d_ioctl_mmap_bo,
};

void
drm_shim_driver_init(void)
{
        shim_device.driver_ioctls = driver_ioctls;
        shim_device.driver_ioctl_count = ARRAY_SIZE(driver_ioctls);

        drm_shim_platform_device_setup("v3d", "/rdb/v3d", "brcm,2711-v3d");

        /* Select the GPU to emulate */
        const char *gpu = debug_get_option("V3D_GPU_ID", "71");

        for (unsigned i = 0; i < ARRAY_SIZE(gpus); i++) {
                if (strncasecmp(gpu, gpus[i].name, strlen(gpus[i].name)) == 0) {
                        shim_gpu = &gpus[i];
                        break;
                }
        }
}
