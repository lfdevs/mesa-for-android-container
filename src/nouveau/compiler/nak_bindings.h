/*
 * Copyright © 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"

#include "nouveau_bo.h"
#include "nouveau_context.h"
#include "nouveau_device.h"

#include <xf86drm.h>
#include "drm-uapi/nouveau_drm.h"

#define DRM_RS_IOCTL(FOO) \
   DRM_RS_IOCTL_##FOO = DRM_IOCTL_##FOO

enum ENUM_PACKED drm_rs_ioctls {
   DRM_RS_IOCTL(NOUVEAU_EXEC),
};
